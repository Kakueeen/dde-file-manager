#ifndef DTAGEDIT_H
#define DTAGEDIT_H

#include <set>

#include "durl.h"
#include "dcrumbedit.h"
#include "darrowrectangle.h"


#include <QFrame>
#include <QLabel>
#include <QTextEdit>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QFocusEvent>

using namespace Dtk::Widget;


class DTagEdit final : public DArrowRectangle
{
    Q_OBJECT
public:
    DTagEdit(QWidget* const parent = nullptr);
    virtual ~DTagEdit()=default;
    DTagEdit(const DTagEdit& other)=delete;
    DTagEdit& operator=(const DTagEdit& other)=delete;

    void setFocusOutSelfClosing(bool value)noexcept;
    void setFilesForTagging(const QList<DUrl>& files);
    void appendCrumb(const QString& crumbText)noexcept;

public slots:
    void onFocusOut();

private:
    void initializeWidgets();
    void initializeParameters();
    void initializeLayout();
    void initializeConnect();

    DCrumbEdit* m_crumbEdit{ nullptr };
    QLabel* m_promptLabel{ nullptr };
    QVBoxLayout* m_totalLayout{ nullptr };
    QFrame* m_BGFrame{ nullptr };

    QList<DUrl> m_files{};

    std::atomic<bool> m_flagForShown{ false };

    mutable std::set<QString> m_initialTags{};
};



#endif // DTAGEDIT_H
