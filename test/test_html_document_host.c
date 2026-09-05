#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "reist/gui/html_document.h"

static reist_html_document_t document;

static int text_contains(const char *needle) {
    size_t length = strlen(needle);
    if (length == 0U || length > document.text_length) return 0;
    for (uint32_t offset = 0U;
         offset + length <= document.text_length; ++offset) {
        if (memcmp(document.text + offset, needle, length) == 0) return 1;
    }
    return 0;
}

static void semantic_document(void) {
    static const uint8_t markup[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<title>REIST &amp; Web</title><style>body { color: red }</style>"
        "</head><body><h1>Bounded <em>Browser</em></h1>"
        "<p>Hello&nbsp;world &lt;safe&gt;.</p>"
        "<ul><li>One</li><li><a href='/next.html'>Next</a></li></ul>"
        "<ol><li>First</li><li>Second</li></ol>"
        "<script>if (one < two) document.write('hidden');</script>"
        "</body></html>";
    assert(reist_html_document_parse(markup, sizeof(markup) - 1U,
                                     &document) == REIST_HTML_OK);
    assert(strcmp(document.title, "REIST & Web") == 0);
    assert(text_contains("Bounded Browser"));
    assert(text_contains("Hello world <safe>."));
    assert(!text_contains("document.write"));
    assert(document.link_count == 1U);
    assert(strcmp(document.links[0].href, "/next.html") == 0);
    assert(document.element_count < 32U);
    uint32_t ordered_markers = 0U;
    for (uint32_t index = 0U; index < document.element_count; ++index)
        if (document.elements[index].kind == REIST_HTML_ELEMENT_LIST_MARKER &&
            document.elements[index].text_length != 0U) ++ordered_markers;
    assert(ordered_markers == 2U);
}

static void coalesces_long_text(void) {
    static uint8_t markup[60000U];
    memset(markup, 'a', sizeof(markup));
    assert(reist_html_document_parse(markup, sizeof(markup), &document) ==
           REIST_HTML_OK);
    assert(document.text_length == sizeof(markup));
    assert(document.element_count == 1U);
}

static void truncates_optional_title_at_utf8_boundary(void) {
    uint8_t markup[REIST_HTML_TITLE_CAPACITY + 96U];
    const size_t prefix = REIST_HTML_TITLE_CAPACITY - 2U;
    memcpy(markup, "<title>", 7U);
    memset(markup + 7U, 'a', prefix);
    static const char tail[] = "\xe2\x82\xac suffix</title><p>Visible body</p>";
    memcpy(markup + 7U + prefix, tail, sizeof(tail));
    assert(reist_html_document_parse(markup, 7U + prefix + sizeof(tail) - 1U, &document) == REIST_HTML_OK);
    assert(strlen(document.title) == prefix && text_contains("Visible body"));
    /* A truncated title does not hide malformed input from validation. */
    markup[10U + prefix] = 0xC0U;
    assert(reist_html_document_parse(markup, 7U + prefix + sizeof(tail) - 1U, &document) == REIST_HTML_ENCODING);
}

static void rejects_invalid_input(void) {
    static const uint8_t bad_utf8[] = {'<', 'p', '>', 0xC0U, 0xAFU};
    assert(reist_html_document_parse(bad_utf8, sizeof(bad_utf8), &document) ==
           REIST_HTML_ENCODING);
    assert(document.element_count == 0U);

    static const uint8_t unclosed_raw[] = "<script>still active";
    assert(reist_html_document_parse(unclosed_raw,
                                     sizeof(unclosed_raw) - 1U,
                                     &document) == REIST_HTML_INVALID);

    static uint8_t nested[256U];
    size_t used = 0U;
    for (uint32_t depth = 0U; depth < REIST_HTML_NESTING_CAPACITY + 1U;
         ++depth) {
        nested[used++] = '<'; nested[used++] = 'b'; nested[used++] = '>';
    }
    assert(reist_html_document_parse(nested, used, &document) ==
           REIST_HTML_CAPACITY);
}

static void resolves_navigation_targets(void) {
    char resolved[REIST_HTML_HREF_CAPACITY];
    assert(reist_html_url_resolve("https://example.test/a/index.html",
           "/next.html", resolved, sizeof(resolved)) == REIST_HTML_OK);
    assert(strcmp(resolved, "https://example.test/next.html") == 0);
    assert(reist_html_url_resolve("https://example.test/a/index.html",
           "part.html", resolved, sizeof(resolved)) == REIST_HTML_OK);
    assert(strcmp(resolved, "https://example.test/a/part.html") == 0);
    assert(reist_html_url_resolve("/htdocs/index.html", "#details",
           resolved, sizeof(resolved)) == REIST_HTML_OK);
    assert(strcmp(resolved, "/htdocs/index.html#details") == 0);
    assert(reist_html_url_resolve("/htdocs/index.html", "javascript:run()",
           resolved, sizeof(resolved)) == REIST_HTML_INVALID);

    assert(reist_html_navigation_normalize(
           "example.test/docs", resolved, sizeof(resolved)) == REIST_HTML_OK);
    assert(strcmp(resolved, "https://example.test/docs") == 0);
    assert(reist_html_navigation_normalize(
           "HTTP://example.test/", resolved, sizeof(resolved)) ==
           REIST_HTML_OK);
    assert(strcmp(resolved, "http://example.test/") == 0);
    assert(reist_html_navigation_normalize(
           "/htdocs/index.html", resolved, sizeof(resolved)) == REIST_HTML_OK);
    assert(strcmp(resolved, "/htdocs/index.html") == 0);
    assert(reist_html_navigation_normalize(
           "javascript:run()", resolved, sizeof(resolved)) ==
           REIST_HTML_INVALID);
    assert(reist_html_navigation_normalize(
           "https://", resolved, sizeof(resolved)) == REIST_HTML_INVALID);
}

int main(void) {
    static const struct { const char *base, *ref, *expected; } cases[] = {
        {"https://example.test", "a", "https://example.test/a"},
        {"https://example.test/a/b?old#x", "../c?x=1#z", "https://example.test/c?x=1#z"},
        {"https://example.test/a/b?old#x", "?new", "https://example.test/a/b?new"},
        {"https://example.test/a/b?old#x", "", "https://example.test/a/b?old"},
        {"https://example.test/a/b", "//cdn.test/p.png", "https://cdn.test/p.png"},
        {"https://example.test/a/b", "./c/../d", "https://example.test/a/d"},
        {"/htdocs/index.html", "../assets/p.bmp", "/assets/p.bmp"},
        {"https://example.test/a", "#details", "https://example.test/a#details"},
        {"https://example.test/a", "/q?next=https://b.test", "https://example.test/q?next=https://b.test"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char resolved[256];
        assert(reist_html_url_resolve(cases[i].base, cases[i].ref,
                                      resolved, sizeof(resolved)) == 0);
        assert(strcmp(resolved, cases[i].expected) == 0);
    }
    semantic_document();
    coalesces_long_text();
    truncates_optional_title_at_utf8_boundary();
    rejects_invalid_input();
    resolves_navigation_targets();
    return 0;
}
