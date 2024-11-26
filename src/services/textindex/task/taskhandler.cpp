// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "taskhandler.h"

#include "progressnotifier.h"
#include "utils/indextraverseutils.h"

#include <dfm-base/base/urlroute.h>
#include <dfm-base/base/device/deviceutils.h>
#include <dfm-base/base/schemefactory.h>
#include <dfm-base/utils/fileutils.h>
#include <dfm-base/utils/finallyutil.h>

#include <docparser.h>

#include <fulltext/chineseanalyzer.h>

#include <lucene++/LuceneHeaders.h>
#include <FileUtils.h>
#include <FilterIndexReader.h>
#include <FuzzyQuery.h>
#include <QueryWrapperFilter.h>

#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QRegularExpression>
#include <QStandardPaths>

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

SERVICETEXTINDEX_USE_NAMESPACE
DFMBASE_USE_NAMESPACE
using namespace Lucene;

namespace {

class ProgressReporter
{
public:
    explicit ProgressReporter()
        : processedCount(0), lastReportTime(QDateTime::currentDateTime())
    {
    }

    ~ProgressReporter()
    {
        // 确保最后一次进度能够显示
        emit ProgressNotifier::instance()->progressChanged(processedCount);
    }

    void increment()
    {
        ++processedCount;

        // 检查是否经过了足够的时间间隔(1秒)
        QDateTime now = QDateTime::currentDateTime();
        if (lastReportTime.msecsTo(now) >= 1000) {   // 1000ms = 1s
            emit ProgressNotifier::instance()->progressChanged(processedCount);
            lastReportTime = now;
        }
    }

private:
    qint64 processedCount;
    QDateTime lastReportTime;
};

// 目录遍历相关函数
using FileHandler = std::function<void(const QString &path)>;

// 常量定义
static constexpr char kSupportFiles[] { "(rtf)|(odt)|(ods)|(odp)|(odg)|(docx)|(xlsx)|(pptx)|(ppsx)|(md)|"
                                        "(xls)|(xlsb)|(doc)|(dot)|(wps)|(ppt)|(pps)|(txt)|(pdf)|(dps)|"
                                        "(sh)|(html)|(htm)|(xml)|(xhtml)|(dhtml)|(shtm)|(shtml)|"
                                        "(json)|(css)|(yaml)|(ini)|(bat)|(js)|(sql)|(uof)|(ofd)" };

// 文档处理相关函数
DocumentPtr createFileDocument(const QString &file)
{
    DocumentPtr doc = newLucene<Document>();

    // file path
    doc->add(newLucene<Field>(L"path", file.toStdWString(),
                              Field::STORE_YES, Field::INDEX_NOT_ANALYZED));

    // file last modified time
    auto info = InfoFactory::create<FileInfo>(QUrl::fromLocalFile(file),
                                              Global::CreateFileInfoType::kCreateFileInfoSync);
    const QDateTime &modifyTime { info->timeOf(TimeInfoType::kLastModified).toDateTime() };
    const QString &modifyEpoch { QString::number(modifyTime.toSecsSinceEpoch()) };
    doc->add(newLucene<Field>(L"modified", modifyEpoch.toStdWString(),
                              Field::STORE_YES, Field::INDEX_NOT_ANALYZED));

    // file contents
    QString contents = DocParser::convertFile(file.toStdString()).c_str();
    doc->add(newLucene<Field>(L"contents", contents.toStdWString(),
                              Field::STORE_YES, Field::INDEX_ANALYZED));

    return doc;
}

bool checkNeedUpdate(const QString &file, const IndexReaderPtr &reader, bool *needAdd)
{
    try {
        SearcherPtr searcher = newLucene<IndexSearcher>(reader);
        TermQueryPtr query = newLucene<TermQuery>(newLucene<Term>(L"path", file.toStdWString()));

        // 文件路径为唯一值，所以搜索一个结果就行了
        TopDocsPtr topDocs = searcher->search(query, 1);
        int32_t numTotalHits = topDocs->totalHits;
        if (numTotalHits == 0) {
            if (needAdd)
                *needAdd = true;
            return true;
        }

        DocumentPtr doc = searcher->doc(topDocs->scoreDocs[0]->doc);
        auto info = InfoFactory::create<FileInfo>(QUrl::fromLocalFile(file),
                                                  Global::CreateFileInfoType::kCreateFileInfoSync);
        if (!info)
            return false;

        const QDateTime &modifyTime { info->timeOf(TimeInfoType::kLastModified).toDateTime() };
        const QString &modifyEpoch { QString::number(modifyTime.toSecsSinceEpoch()) };
        const String &storeTime { doc->get(L"modified") };

        return modifyEpoch.toStdWString() != storeTime;
    } catch (const std::exception &e) {
        fmWarning() << "Check update failed:" << file << e.what();
        return false;
    }
}

bool isSupportedFile(const QString &path)
{
    auto info = InfoFactory::create<FileInfo>(QUrl::fromLocalFile(path),
                                              Global::CreateFileInfoType::kCreateFileInfoSync);
    if (!info) return false;

    QString suffix = info->nameOf(NameInfoType::kSuffix);
    static const QRegularExpression suffixRegex(kSupportFiles);
    return suffixRegex.match(suffix).hasMatch();
}

void processFile(const QString &path, const IndexWriterPtr &writer, ProgressReporter *reporter)
{
    try {
        if (!isSupportedFile(path))
            return;

        fmDebug() << "Adding [" << path << "]";
        writer->addDocument(createFileDocument(path));
        if (reporter) {
            reporter->increment();
        }
    } catch (const std::exception &e) {
        fmWarning() << "Process file failed:" << path << e.what();
    }
}

void updateFile(const QString &path, const IndexReaderPtr &reader,
                const IndexWriterPtr &writer, ProgressReporter *reporter)
{
    try {
        if (!isSupportedFile(path))
            return;

        bool needAdd = false;
        if (checkNeedUpdate(path, reader, &needAdd)) {
            if (needAdd) {
                fmDebug() << "Adding new file:" << path;
                writer->addDocument(createFileDocument(path));
            } else {
                fmDebug() << "Updating file:" << path;
                TermPtr term = newLucene<Term>(L"path", path.toStdWString());
                writer->updateDocument(term, createFileDocument(path));
            }
            if (reporter) {
                reporter->increment();
            }
        }
    } catch (const std::exception &e) {
        fmWarning() << "Update file failed:" << path << e.what();
    }
}

void traverseDirectoryCommon(const QString &rootPath, const std::atomic_bool &running,
                             const FileHandler &fileHandler)
{
    QMap<QString, QString> bindPathTable = DeviceUtils::fstabBindInfo();
    QSet<QString> visitedDirs;

    QQueue<QString> dirQueue;
    dirQueue.enqueue(rootPath);

    while (!dirQueue.isEmpty() && running.load()) {
        QString currentPath = dirQueue.dequeue();

        // 检查是否是系统目录或绑定目录
        if (bindPathTable.contains(currentPath) || IndexTraverseUtils::shouldSkipDirectory(currentPath))
            continue;

        // 检查路径长度和深度限制
        if (currentPath.size() > FILENAME_MAX - 1 || currentPath.count('/') > 20)
            continue;

        // 检查目录是否已访问
        if (!IndexTraverseUtils::isValidDirectory(currentPath, visitedDirs))
            continue;

        DIR *dir = opendir(currentPath.toStdString().c_str());
        if (!dir) {
            fmWarning() << "Cannot open directory:" << currentPath;
            continue;
        }

        FinallyUtil dirCloser([dir]() { closedir(dir); });

        struct dirent *entry;
        while ((entry = readdir(dir)) && running.load()) {
            if (IndexTraverseUtils::isHiddenFile(entry->d_name) || IndexTraverseUtils::isSpecialDir(entry->d_name))
                continue;

            QString fullPath = QDir::cleanPath(currentPath + QDir::separator() + QString::fromUtf8(entry->d_name));

            struct stat st;
            if (lstat(fullPath.toStdString().c_str(), &st) == -1)
                continue;

            // 对于普通文件，只检查路径有效性
            if (S_ISREG(st.st_mode)) {
                if (IndexTraverseUtils::isValidFile(fullPath)) {
                    fileHandler(fullPath);
                }
            }
            // 对于目录，加入队列（后续会检查是否访问过）
            else if (S_ISDIR(st.st_mode)) {
                dirQueue.enqueue(fullPath);
            }
        }
    }
}

void traverseDirectory(const QString &rootPath, const IndexWriterPtr &writer,
                       const std::atomic_bool &running)
{
    ProgressReporter reporter;
    traverseDirectoryCommon(rootPath, running, [&](const QString &path) {
        processFile(path, writer, &reporter);
    });
}

void traverseForUpdate(const QString &rootPath, const IndexReaderPtr &reader,
                       const IndexWriterPtr &writer, const std::atomic_bool &running)
{
    ProgressReporter reporter;
    traverseDirectoryCommon(rootPath, running, [&](const QString &path) {
        updateFile(path, reader, writer, &reporter);
    });
}

}   // namespace

// 公开的任务处理函数实现
TaskHandler TaskHandlers::CreateIndexHandler()
{
    return [](const QString &path, std::atomic_bool &running) -> bool {
        fmInfo() << "Creating index for path:" << path;

        QDir dir;
        if (!dir.exists(path)) {
            fmWarning() << "Source directory doesn't exist:" << path;
            return false;
        }

        if (!dir.exists(indexStorePath())) {
            if (!dir.mkpath(indexStorePath())) {
                fmWarning() << "Unable to create index directory:" << indexStorePath();
                return false;
            }
        }

        try {
            IndexWriterPtr writer = newLucene<IndexWriter>(
                    FSDirectory::open(indexStorePath().toStdWString()),
                    newLucene<ChineseAnalyzer>(),
                    true,
                    IndexWriter::MaxFieldLengthLIMITED);

            FinallyUtil writerCloser([&writer]() {
                try {
                    if (writer) writer->close();
                } catch (...) {
                }
            });

            fmInfo() << "Indexing to directory:" << indexStorePath();

            writer->deleteAll();
            traverseDirectory(path, writer, running);

            if (!running.load()) {
                fmInfo() << "Create index task was interrupted";
                return false;
            }

            writer->optimize();
            return true;
        } catch (const LuceneException &e) {
            fmWarning() << "Create index failed with Lucene exception:"
                        << QString::fromStdWString(e.getError());
        } catch (const std::exception &e) {
            fmWarning() << "Create index failed with exception:" << e.what();
        }

        return false;
    };
}

TaskHandler TaskHandlers::UpdateIndexHandler()
{
    return [](const QString &path, std::atomic_bool &running) -> bool {
        fmInfo() << "Updating index for path:" << path;

        try {
            IndexReaderPtr reader = IndexReader::open(
                    FSDirectory::open(indexStorePath().toStdWString()), true);

            FinallyUtil readerCloser([&reader]() {
                try {
                    if (reader) reader->close();
                } catch (...) {
                }
            });

            IndexWriterPtr writer = newLucene<IndexWriter>(
                    FSDirectory::open(indexStorePath().toStdWString()),
                    newLucene<ChineseAnalyzer>(),
                    false,
                    IndexWriter::MaxFieldLengthLIMITED);

            FinallyUtil writerCloser([&writer]() {
                try {
                    if (writer) writer->close();
                } catch (...) {
                }
            });

            traverseForUpdate(path, reader, writer, running);

            writer->optimize();
            return true;
        } catch (const LuceneException &e) {
            fmWarning() << "Update index failed with Lucene exception:"
                        << QString::fromStdWString(e.getError());
        } catch (const std::exception &e) {
            fmWarning() << "Update index failed with exception:" << e.what();
        }

        return false;
    };
}
