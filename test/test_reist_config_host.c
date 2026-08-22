#include <assert.h>
#include <string.h>

#include "reist/config.h"

static void test_valid_parse_get_set_and_preservation(void) {
    static const char input[] =
        "# kept comment semantics are intentionally not serialized\n"
        "schema=reist.input/1\n"
        "keyboard.layout=de\n"
        "future.option=preserved value\n";
    reist_config_document_t document;
    assert(reist_config_parse(input, sizeof(input) - 1U,
                              "reist.input/1", &document) == 0);
    assert(document.entry_count == 2U);
    assert(strcmp(reist_config_get(&document, "keyboard.layout"), "de") == 0);
    assert(reist_config_set(&document, "keyboard.layout", "us") == 0);
    assert(strcmp(reist_config_get(&document, "keyboard.layout"), "us") == 0);
    char output[REIST_CONFIG_FILE_CAPACITY];
    size_t length = 0U;
    assert(reist_config_serialize(&document, output, sizeof(output),
                                  &length) == 0);
    assert(length == strlen(output));
    assert(strstr(output, "schema=reist.input/1\n") == output);
    assert(strstr(output, "future.option=preserved value\n") != 0);
}

static void test_fail_closed_inputs(void) {
    reist_config_document_t document;
    static const char duplicate[] =
        "schema=reist.input/1\nkeyboard.layout=de\nkeyboard.layout=us\n";
    assert(reist_config_parse(duplicate, sizeof(duplicate) - 1U,
                              "reist.input/1", &document) != 0);
    assert(document.entry_count == 0U);
    static const char wrong_schema[] =
        "schema=reist.desktop/1\ntheme=classic\n";
    assert(reist_config_parse(wrong_schema, sizeof(wrong_schema) - 1U,
                              "reist.input/1", &document) != 0);
    static const char partial[] =
        "schema=reist.input/1\nkeyboard.layout=de";
    assert(reist_config_parse(partial, sizeof(partial) - 1U,
                              "reist.input/1", &document) != 0);
    static const char schema_not_first[] =
        "keyboard.layout=de\nschema=reist.input/1\n";
    assert(reist_config_parse(schema_not_first, sizeof(schema_not_first) - 1U,
                              "reist.input/1", &document) != 0);
}

static void test_capacity_and_character_validation(void) {
    reist_config_document_t document;
    assert(reist_config_parse("", 0U, "reist.input/1", &document) != 0);
    static const char bad_key[] =
        "schema=reist.input/1\nkeyboard layout=de\n";
    assert(reist_config_parse(bad_key, sizeof(bad_key) - 1U,
                              "reist.input/1", &document) != 0);
    char oversized[REIST_CONFIG_FILE_CAPACITY + 2U];
    memset(oversized, 'x', sizeof(oversized));
    assert(reist_config_parse(oversized, sizeof(oversized),
                              "reist.input/1", &document) != 0);
}

int main(void) {
    test_valid_parse_get_set_and_preservation();
    test_fail_closed_inputs();
    test_capacity_and_character_validation();
    return 0;
}
