/*
   FSearch - A fast file search utility
   Copyright © 2020 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
   */

#define _GNU_SOURCE
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <fnmatch.h>

#include "database_search.h"
#include "string_utils.h"
#include "query.h"
//#include "debug.h"
#include "utf8.h"

#define OVECCOUNT 3

typedef struct search_query_s
{
    char *query;
    uint32_t (*search_func)(const char *, const char *);
    size_t query_len;
    uint32_t has_uppercase;
    uint32_t has_separator;
    uint32_t is_utf8;
    //uint32_t found;
} search_query_t;

typedef struct search_context_s
{
    DatabaseSearch *search;
    BTreeNode **results;
    search_query_t **queries;
    uint32_t num_queries;
    uint32_t num_results;
    uint32_t start_pos;
    uint32_t end_pos;
} search_thread_context_t;

DatabaseSearchResult *
db_search(DatabaseSearch *search, FsearchQuery *q);

static DatabaseSearchResult *
db_search_empty(DatabaseSearch *search);

DatabaseSearchEntry *
db_search_entry_new(BTreeNode *node, uint32_t pos);

static void
db_search_entry_free(DatabaseSearchEntry *entry);

static gpointer
fsearch_search_thread(gpointer user_data)
{
    DatabaseSearch *search = user_data;

    g_mutex_lock(&search->query_mutex);
    while (true) {
        search->search_thread_started = true;
        g_cond_wait(&search->search_thread_start_cond, &search->query_mutex);
        printf("---------------------------g_cond_wait (&search->search_thread_start_cond)\n");
        if (search->search_thread_terminate) {
            break;
        }
        while (true) {
            FsearchQuery *query = search->query_ctx;
            if (!query) {
                break;
            }
            search->query_ctx = NULL;
            g_mutex_unlock(&search->query_mutex);
            // if query is empty string we are done here
            DatabaseSearchResult *result = NULL;
            if (fs_str_is_empty(query->query)) {
                if (!search->hide_results) {
                    result = db_search_empty(search);
                } else {
                    result = calloc(1, sizeof(DatabaseSearchResult));
                }
            } else {
                result = db_search(search, query);
            }
            result->cb_data = query->callback_data;
            query->callback(result, query->sender);
            printf("+++++++++++++++++++++++++++++++++++++++++++query->callback\n");

            //free
            if (result->results)
                g_ptr_array_free(result->results, TRUE);
            free(result);
            result = NULL;

            fsearch_query_free(query);
            g_mutex_lock(&search->query_mutex);
        }
    }
    g_mutex_unlock(&search->query_mutex);
    return NULL;
}

static search_thread_context_t *
search_thread_context_new(DatabaseSearch *search,
                          search_query_t **queries,
                          uint32_t num_queries,
                          uint32_t start_pos,
                          uint32_t end_pos)
{
    search_thread_context_t *ctx = calloc(1, sizeof(search_thread_context_t));
    assert(ctx != NULL);
    assert(end_pos >= start_pos);

    ctx->search = search;
    ctx->queries = queries;
    ctx->num_queries = num_queries;
    ctx->results = calloc(end_pos - start_pos + 1, sizeof(BTreeNode *));
    assert(ctx->results != NULL);

    ctx->num_results = 0;
    ctx->start_pos = start_pos;
    ctx->end_pos = end_pos;
    return ctx;
}

static inline bool
filter_node(BTreeNode *node, FsearchFilter filter)
{
    if (filter == FSEARCH_FILTER_NONE) {
        return true;
    }
    bool is_dir = node->is_dir;
    if (filter == FSEARCH_FILTER_FILES
        && !is_dir) {
        return true;
    }
    if (filter == FSEARCH_FILTER_FOLDERS
        && is_dir) {
        return true;
    }
    return false;
}

static void *
search_thread(void *user_data)
{
    search_thread_context_t *ctx = (search_thread_context_t *)user_data;
    //    assert (ctx != NULL);
    //    assert (ctx->results != NULL);
    if (ctx == NULL) {
        return NULL;
    }
    if (ctx->results == NULL) {
        return NULL;
    }
    const uint32_t start = ctx->start_pos;
    const uint32_t end = ctx->end_pos;
    const uint32_t max_results = ctx->search->max_results;
    const uint32_t num_queries = ctx->num_queries;
    const FsearchFilter filter = ctx->search->filter;
    search_query_t **queries = ctx->queries;
    const uint32_t search_in_path = ctx->search->search_in_path;
    const uint32_t auto_search_in_path = ctx->search->auto_search_in_path;
    DynamicArray *entries = ctx->search->entries;
    BTreeNode **results = ctx->results;

    uint32_t num_results = 0;
    char full_path[PATH_MAX] = "";
    for (uint32_t i = start; i <= end; i++) {
        if (max_results && num_results == max_results) {
            break;
        }
        BTreeNode *node = darray_get_item(entries, i);
        if (!node) {
            continue;
        }
        if (!filter_node(node, filter)) {
            continue;
        }

        const char *haystack_path = NULL;
        const char *haystack_name = node->name;
        if (search_in_path) {
            btree_node_get_path_full(node, full_path, sizeof(full_path));
            haystack_path = full_path;
        }

        uint32_t num_found = 0;
        while (true) {
            if (num_found == num_queries) {
                results[num_results] = node;
                num_results++;
                break;
            }
            search_query_t *query = queries[num_found++];
            if (!query) {
                break;
            }
            char *ptr = query->query;
            uint32_t (*search_func)(const char *, const char *) = query->search_func;

            const char *haystack = NULL;
            if (search_in_path || (auto_search_in_path && query->has_separator)) {
                if (!haystack_path) {
                    btree_node_get_path_full(node, full_path, sizeof(full_path));
                    haystack_path = full_path;
                }
                haystack = haystack_path;
            } else {
                haystack = haystack_name;
            }
            if (!search_func(haystack, ptr)) {
                if (ctx->search->enable_py && strlen(node->full_py_name)) {
                    // search first pinyin
                    if (!search_func(node->first_py_name, ptr)) {
                        // search full pinyin
                        if (!search_func(node->full_py_name, ptr)) {
                            break;
                        }
                    }
                } else {
                    break;
                }
            }
        }
    }
    ctx->num_results = num_results;
    return NULL;
}

#ifdef DEBUG
static struct timeval tm1;
#endif

static inline void timer_start()
{
#ifdef DEBUG
    gettimeofday(&tm1, NULL);
#endif
}

static inline void timer_stop()
{
#ifdef DEBUG
    struct timeval tm2;
    gettimeofday(&tm2, NULL);

    unsigned long long t = 1000 * (tm2.tv_sec - tm1.tv_sec) + (tm2.tv_usec - tm1.tv_usec) / 1000;
    trace("%llu ms\n", t);
#endif
}

static void *
search_regex_thread(void *user_data)
{
    search_thread_context_t *ctx = (search_thread_context_t *)user_data;
    assert(ctx != NULL);
    assert(ctx->results != NULL);

    search_query_t **queries = ctx->queries;
    search_query_t *query = queries[0];
    const char *error;
    PCRE2_SIZE erroffset;
    int errornumber;
    uint32_t options = ctx->search->match_case ? 0 : PCRE2_CASELESS;
    pcre2_code *regex = pcre2_compile((PCRE2_SPTR)query->query,
                                      PCRE2_ZERO_TERMINATED,
                                      options,
                                      &errornumber,
                                      &erroffset,
                                      NULL);

    int ovector[OVECCOUNT];

    if (regex) {
        const uint32_t start = ctx->start_pos;
        const uint32_t end = ctx->end_pos;
        const uint32_t max_results = ctx->search->max_results;
        const bool search_in_path = ctx->search->search_in_path;
        const bool auto_search_in_path = ctx->search->auto_search_in_path;
        DynamicArray *entries = ctx->search->entries;
        BTreeNode **results = ctx->results;
        const FsearchFilter filter = ctx->search->filter;

        uint32_t num_results = 0;
        char full_path[PATH_MAX] = "";
        for (uint32_t i = start; i <= end; ++i) {
            if (max_results && num_results == max_results) {
                break;
            }
            BTreeNode *node = darray_get_item(entries, i);
            if (!node) {
                continue;
            }

            if (!filter_node(node, filter)) {
                continue;
            }

            const char *haystack = NULL;
            if (search_in_path || (auto_search_in_path && query->has_separator)) {
                btree_node_get_path_full(node, full_path, sizeof(full_path));
                haystack = full_path;
            } else {
                haystack = node->name;
            }
            size_t haystack_len = strlen(haystack);
            pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
            
            if (pcre2_match(regex, (PCRE2_SPTR)haystack, haystack_len,
                            0, 0, match_data, NULL) 
                >= 0) {
                    results[num_results++] = node;
                } else if (ctx->search->enable_py && strlen(node->full_py_name)) {
                    if (pcre2_match(regex, (PCRE2_SPTR)node->first_py_name,
                                    strlen(node->first_py_name),
                                    0, 0, match_data, NULL)
                        >= 0) {
                        results[num_results++] = node;
                    } else if (pcre2_match(regex, (PCRE2_SPTR)node->full_py_name,
                                           strlen(node->full_py_name),
                                           0, 0, match_data, NULL)
                               >= 0) {
                        results[num_results++] = node;
                }
            }
        }
        ctx->num_results = num_results;
        pcre2_code_free(regex);
    }
    return NULL;
}

static int
is_regex(const char *query)
{
    char regex_chars[] = {
        '$',
        '(',
        ')',
        '*',
        '+',
        '.',
        '?',
        '[',
        '\\',
        '^',
        '{',
        '|',
        '\0'
    };

    return (strpbrk(query, regex_chars) != NULL);
}

static uint32_t
search_wildcard_icase(const char *haystack, const char *needle)
{
    return !fnmatch(needle, haystack, FNM_CASEFOLD) ? 1 : 0;
}

static uint32_t
search_wildcard(const char *haystack, const char *needle)
{
    return !fnmatch(needle, haystack, 0) ? 1 : 0;
}

static uint32_t
search_normal_icase_u8(const char *haystack, const char *needle)
{
    // TODO: make this faster
    return utf8casestr(haystack, needle) ? 1 : 0;
}

static uint32_t
search_normal_icase(const char *haystack, const char *needle)
{
    return strcasestr(haystack, needle) ? 1 : 0;
}

static uint32_t
search_normal(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) ? 1 : 0;
}

static void
search_query_free(void *data)
{
    search_query_t *query = data;
    assert(query != NULL);

    if (query->query != NULL) {
        g_free(query->query);
        query->query = NULL;
    }
    g_free(query);
    query = NULL;
}

static search_query_t *
search_query_new(const char *query, bool match_case)
{
    search_query_t *new = calloc(1, sizeof(search_query_t));
    assert(new != NULL);

    new->query = g_strdup(query);
    new->query_len = strlen(query);
    new->has_uppercase = fs_str_has_upper(query);
    new->has_separator = strchr(query, '/') ? 1 : 0;
    // TODO: this might not work at all times?
    if (utf8len(query) != new->query_len) {
        new->is_utf8 = 1;
    } else {
        new->is_utf8 = 0;
    }

    if (match_case) {
        new->search_func = search_normal;
    } else {
        if (new->is_utf8) {
            new->search_func = search_normal_icase_u8;
        } else {
            new->search_func = search_normal_icase;
        }
    }

    return new;
}

static search_query_t **
build_queries(DatabaseSearch *search, FsearchQuery *q)
{
    assert(search != NULL);
    assert(q->query != NULL);

    char *tmp_query_copy = strdup(q->query);
    assert(tmp_query_copy != NULL);
    // remove leading/trailing whitespace
    g_strstrip(tmp_query_copy);

    search_query_t **queries = calloc(2, sizeof(search_query_t *));
    queries[0] = search_query_new(tmp_query_copy, search->match_case);
    queries[1] = NULL;
    g_free(tmp_query_copy);
    tmp_query_copy = NULL;

    return queries;
}

static DatabaseSearchResult *
db_search_empty(DatabaseSearch *search)
{
    assert(search != NULL);
    assert(search->entries != NULL);

    const uint32_t num_results = MIN(search->max_results, search->num_entries);
    GPtrArray *results = g_ptr_array_sized_new(num_results);
    g_ptr_array_set_free_func(results, (GDestroyNotify)db_search_entry_free);

    DynamicArray *entries = search->entries;

    uint32_t num_folders = 0;
    uint32_t num_files = 0;
    uint32_t pos = 0;
    for (uint32_t i = 0; pos < num_results && i < search->num_entries; ++i) {
        BTreeNode *node = darray_get_item(entries, i);
        if (!node) {
            continue;
        }

        if (!filter_node(node, search->filter)) {
            continue;
        }
        if (node->is_dir) {
            num_folders++;
        } else {
            num_files++;
        }
        DatabaseSearchEntry *entry = db_search_entry_new(node, pos);
        g_ptr_array_add(results, entry);
        pos++;
    }
    DatabaseSearchResult *result_ctx = calloc(1, sizeof(DatabaseSearchResult));
    assert(result_ctx != NULL);
    result_ctx->results = results;
    result_ctx->num_folders = num_folders;
    result_ctx->num_files = num_files;
    return result_ctx;
}

DatabaseSearchResult *
db_search(DatabaseSearch *search, FsearchQuery *q)
{
    assert(search != NULL);
    assert(search->entries != NULL);
    if (search->num_entries == 0) {
        DatabaseSearchResult *result_ctx = calloc(1, sizeof(DatabaseSearchResult));
        result_ctx->results = 0;
        result_ctx->num_folders = 0;
        result_ctx->num_files = 0;
        return result_ctx;
    }

    search_query_t **queries = build_queries(search, q);

    const uint32_t num_threads = MIN(fsearch_thread_pool_get_num_threads(search->pool), search->num_entries);
    const uint32_t num_items_per_thread = MAX(search->num_entries / num_threads, 1);

    search_thread_context_t *thread_data[num_threads];
    memset(thread_data, 0, num_threads * sizeof(search_thread_context_t *));

    const uint32_t max_results = search->max_results;
    const bool limit_results = max_results ? true : false;
    const bool is_reg = is_regex(search->query);
    uint32_t num_queries = 0;
    while (queries[num_queries]) {
        num_queries++;
    }
    uint32_t start_pos = 0;
    uint32_t end_pos = num_items_per_thread - 1;
    timer_start();
    GList *temp = fsearch_thread_pool_get_threads(search->pool);
    for (uint32_t i = 0; i < num_threads; i++) {
        thread_data[i] = search_thread_context_new(search,
                                                   queries,
                                                   num_queries,
                                                   start_pos,
                                                   i == num_threads - 1 ? search->num_entries - 1 : end_pos);

        start_pos = end_pos + 1;
        end_pos += num_items_per_thread;

        fsearch_thread_pool_push_data(search->pool,
                                      temp,
                                      is_reg && search->enable_regex ? search_regex_thread : search_thread,
                                      thread_data[i]);
        temp = temp->next;
    }
    temp = fsearch_thread_pool_get_threads(search->pool);
    while (temp) {
        fsearch_thread_pool_wait_for_thread(search->pool, temp);
        temp = temp->next;
    }

    //    trace ("search done: ");
    timer_stop();

    // get total number of entries found
    uint32_t num_results = 0;
    for (uint32_t i = 0; i < num_threads; ++i) {
        num_results += thread_data[i]->num_results;
    }
    GPtrArray *results = g_ptr_array_sized_new(MIN(num_results, max_results));
    g_ptr_array_set_free_func(results, (GDestroyNotify)db_search_entry_free);
    uint32_t num_folders = 0;
    uint32_t num_files = 0;

    uint32_t pos = 0;
    for (uint32_t i = 0; i < num_threads; i++) {
        search_thread_context_t *ctx = thread_data[i];
        if (!ctx) {
            break;
        }
        for (uint32_t j = 0; j < ctx->num_results; ++j) {
            if (limit_results) {
                if (pos >= max_results) {
                    break;
                }
            }
            BTreeNode *node = ctx->results[j];
            if (node->is_dir) {
                num_folders++;
            } else {
                num_files++;
            }
            DatabaseSearchEntry *entry = db_search_entry_new(node, pos);
            g_ptr_array_add(results, entry);
            pos++;
        }
        if (ctx->results) {
            g_free(ctx->results);
            ctx->results = NULL;
        }
        if (ctx) {
            g_free(ctx);
            ctx = NULL;
        }
    }
    for (uint32_t i = 0; i < num_queries; ++i) {
        search_query_free(queries[i]);
        queries[i] = NULL;
    }
    free(queries);
    queries = NULL;
    DatabaseSearchResult *result_ctx = calloc(1, sizeof(DatabaseSearchResult));
    assert(result_ctx != NULL);
    result_ctx->results = results;
    result_ctx->num_folders = num_folders;
    result_ctx->num_files = num_files;
    return result_ctx;
}

void db_search_results_clear(DatabaseSearch *search)
{
    assert(search != NULL);

    // free entries
    if (search->results) {
        g_ptr_array_free(search->results, TRUE);
        search->results = NULL;
    }
    search->num_folders = 0;
    search->num_files = 0;
    return;
}

void db_search_free(DatabaseSearch *search)
{
    assert(search != NULL);

    db_search_results_clear(search);
    if (search->query) {
        g_free(search->query);
        search->query = NULL;
    }
    g_mutex_lock(&search->query_mutex);
    if (search->query_ctx) {
        fsearch_query_free(search->query_ctx);
        search->query_ctx = NULL;
    }
    g_mutex_unlock(&search->query_mutex);

    search->search_thread_terminate = true;
    g_cond_signal(&search->search_thread_start_cond);
    g_thread_join(search->search_thread);
    g_mutex_clear(&search->query_mutex);
    g_cond_clear(&search->search_thread_start_cond);
    g_free(search);
    search = NULL;
    return;
}

BTreeNode *
db_search_entry_get_node(DatabaseSearchEntry *entry)
{
    return entry->node;
}

uint32_t
db_search_entry_get_pos(DatabaseSearchEntry *entry)
{
    return entry->pos;
}

void db_search_entry_set_pos(DatabaseSearchEntry *entry, uint32_t pos)
{
    entry->pos = pos;
}

static void
db_search_entry_free(DatabaseSearchEntry *entry)
{
    if (entry) {
        g_free(entry);
        entry = NULL;
    }
}

DatabaseSearchEntry *
db_search_entry_new(BTreeNode *node, uint32_t pos)
{
    DatabaseSearchEntry *entry = calloc(1, sizeof(DatabaseSearchEntry));
    assert(entry != NULL);

    entry->node = node;
    entry->pos = pos;
    return entry;
}

DatabaseSearch *
db_search_new(FsearchThreadPool *pool)
{
    DatabaseSearch *db_search = calloc(1, sizeof(DatabaseSearch));
    assert(db_search != NULL);

    db_search->pool = pool;
    db_search->search_thread_started = false;
    g_mutex_init(&db_search->query_mutex);
    g_cond_init(&db_search->search_thread_start_cond);
    db_search->search_thread = g_thread_new("fsearch_search_thread", fsearch_search_thread, db_search);
    return db_search;
}

void db_search_set_search_in_path(DatabaseSearch *search, bool search_in_path)
{
    assert(search != NULL);

    search->search_in_path = search_in_path;
}

void db_search_set_query(DatabaseSearch *search, const char *query)
{
    assert(search != NULL);

    if (search->query) {
        g_free(search->query);
    }
    search->query = g_strdup(query);
}

void db_search_update(DatabaseSearch *search,
                      DynamicArray *entries,
                      uint32_t num_entries,
                      uint32_t max_results,
                      FsearchFilter filter,
                      const char *query,
                      bool hide_results,
                      bool match_case,
                      bool enable_regex,
                      bool auto_search_in_path,
                      bool search_in_path,
                      bool enable_py)
{
    assert(search != NULL);

    search->entries = entries;
    search->num_entries = num_entries;
    db_search_set_query(search, query);
    search->enable_regex = enable_regex;
    search->search_in_path = search_in_path;
    search->auto_search_in_path = auto_search_in_path;
    search->hide_results = hide_results;
    search->match_case = match_case;
    search->max_results = max_results;
    search->filter = filter;
    search->enable_py = enable_py;
}

uint32_t
db_search_get_num_results(DatabaseSearch *search)
{
    assert(search != NULL);
    return search->results->len;
}

uint32_t
db_search_get_num_files(DatabaseSearch *search)
{
    assert(search != NULL);
    return search->num_files;
}

uint32_t
db_search_get_num_folders(DatabaseSearch *search)
{
    assert(search != NULL);
    return search->num_folders;
}

static void
update_index(DatabaseSearch *search)
{
    assert(search != NULL);

    for (uint32_t i = 0; i < search->results->len; ++i) {
        DatabaseSearchEntry *entry = g_ptr_array_index(search->results, i);
        entry->pos = i;
    }
}

void db_search_remove_entry(DatabaseSearch *search, DatabaseSearchEntry *entry)
{
    if (search == NULL) {
        return;
    }
    if (entry == NULL) {
        return;
    }

    g_ptr_array_remove(search->results, (void *)entry);
    update_index(search);
}

GPtrArray *
db_search_get_results(DatabaseSearch *search)
{
    assert(search != NULL);
    return search->results;
}

static void
db_search_queue(DatabaseSearch *search, FsearchQuery *query)
{
    g_mutex_lock(&search->query_mutex);
    if (search->query_ctx) {
        fsearch_query_free(search->query_ctx);
    }
    search->query_ctx = query;

    g_mutex_unlock(&search->query_mutex);
    // 确保search线程处于wait状态，否则会照成流程阻塞
    while (!search->search_thread_started) {
        usleep(100);
    }
    g_cond_signal(&search->search_thread_start_cond);

    printf("---------------------------g_cond_signal (&search->search_thread_start_cond)\n");
}

void db_perform_search(DatabaseSearch *search, void (*callback)(void *, void *), void *callback_data, void *sender)
{
    assert(search != NULL);
    if (search->entries == NULL) {
        return;
    }

    db_search_results_clear(search);

    FsearchQuery *q = fsearch_query_new(search->query, callback, callback_data, sender, false, false, false, false);
    db_search_queue(search, q);
}
