#include "test_common.h"

#include <locale.h>

#include "attrib.h"
#include "unistr.h"

ntfschar AT_UNNAMED[] = { const_cpu_to_le16('\0') };

static void ascii_to_ntfs(ntfschar *out, const char *text)
{
	size_t i;

	for (i = 0; text[i]; i++)
		out[i] = cpu_to_le16((u16)(unsigned char)text[i]);
	out[i] = const_cpu_to_le16('\0');
}

static void test_name_comparison_and_case_mapping(void)
{
	ntfschar *upcase = NULL;
	ntfschar *locase = NULL;
	u32 upcase_len;
	ntfschar lower[8];
	ntfschar upper[8];
	ntfschar collate_left[8];
	ntfschar collate_right[8];

	upcase_len = ntfs_upcase_build_default(&upcase);
	TEST_ASSERT_EQ_INT((int)upcase_len, 65536);
	locase = ntfs_locase_table_build(upcase, upcase_len);
	TEST_ASSERT(locase != NULL);

	ascii_to_ntfs(lower, "abc");
	ascii_to_ntfs(upper, "ABC");
	TEST_ASSERT(!ntfs_names_are_equal(lower, 3, upper, 3,
		CASE_SENSITIVE, upcase, upcase_len));
	TEST_ASSERT(ntfs_names_are_equal(lower, 3, upper, 3,
		IGNORE_CASE, upcase, upcase_len));
	TEST_ASSERT_EQ_INT(ntfs_names_full_collate(lower, 3, upper, 3,
		CASE_SENSITIVE, upcase, upcase_len), 1);

	ascii_to_ntfs(collate_left, "abc");
	ascii_to_ntfs(collate_right, "BCD");
	TEST_ASSERT_EQ_INT(ntfs_names_full_collate(collate_left, 3,
		collate_right, 3, CASE_SENSITIVE, upcase, upcase_len), -1);

	ascii_to_ntfs(lower, "mIxEd");
	ntfs_name_upcase(lower, 5, upcase, upcase_len);
	ascii_to_ntfs(upper, "MIXED");
	TEST_ASSERT(ntfs_names_are_equal(lower, 5, upper, 5,
		CASE_SENSITIVE, upcase, upcase_len));

	ntfs_name_locase(upper, 5, locase, upcase_len);
	ascii_to_ntfs(lower, "mixed");
	TEST_ASSERT(ntfs_names_are_equal(upper, 5, lower, 5,
		CASE_SENSITIVE, upcase, upcase_len));

	free(locase);
	free(upcase);
}

static void test_string_conversion_and_reserved_names(void)
{
	ntfschar *upcase = NULL;
	u32 upcase_len;
	ntfschar *ucs = NULL;
	ntfschar *unnamed = NULL;
	ntfs_volume vol;
	char *text = NULL;
	int len = -1;
	ntfschar good_name[16];
	ntfschar bad_name[16];
	ntfschar con_name[8];
	ntfschar com_name[16];

	memset(&vol, 0, sizeof(vol));
	upcase_len = ntfs_upcase_build_default(&upcase);
	TEST_ASSERT(upcase != NULL);
	vol.upcase = upcase;
	vol.upcase_len = upcase_len;

	TEST_ASSERT_EQ_INT(ntfs_mbstoucs("Test42", &ucs), 6);
	TEST_ASSERT(ucs != NULL);
	TEST_ASSERT_EQ_INT(ntfs_ucstombs(ucs, 6, &text, 0), 6);
	TEST_ASSERT(strcmp(text, "Test42") == 0);
	free(text);
	free(ucs);

	unnamed = ntfs_str2ucs(NULL, &len);
	TEST_ASSERT(unnamed == AT_UNNAMED);
	TEST_ASSERT_EQ_INT(len, 0);

	ascii_to_ntfs(good_name, "alpha");
	ascii_to_ntfs(bad_name, "bad:name");
	ascii_to_ntfs(con_name, "CON");
	ascii_to_ntfs(com_name, "com1.txt");

	TEST_ASSERT(!ntfs_forbidden_chars(good_name, 5, TRUE));
	TEST_ASSERT(ntfs_forbidden_chars(bad_name, 8, TRUE));
	TEST_ASSERT(!ntfs_forbidden_names(&vol, good_name, 5, TRUE));
	TEST_ASSERT(ntfs_forbidden_names(&vol, con_name, 3, TRUE));
	TEST_ASSERT(ntfs_forbidden_names(&vol, com_name, 8, TRUE));

	free(upcase);
}

static void test_unistr_corpus_case(const char *path __attribute__((unused)),
		const char *name, const unsigned char *data, size_t size,
		void *opaque)
{
	ntfs_volume *vol = (ntfs_volume*)opaque;
	char *input;
	ntfschar *ucs = NULL;
	char *roundtrip = NULL;
	int expected_forbidden = test_name_has_prefix(name, "bad_")
		|| test_name_has_prefix(name, "reserved_");

	input = (char*)malloc(size + 1);
	TEST_ASSERT(input != NULL);
	memcpy(input, data, size);
	input[size] = '\0';

	TEST_ASSERT(ntfs_mbstoucs(input, &ucs) >= 0);
	TEST_ASSERT(ucs != NULL);
	TEST_ASSERT_EQ_INT(ntfs_ucstombs(ucs, (int)ntfs_ucsnlen(ucs, 255),
		&roundtrip, 0) >= 0, 1);
	TEST_ASSERT_EQ_INT(ntfs_forbidden_names(vol, ucs,
		(int)ntfs_ucsnlen(ucs, 255), TRUE), expected_forbidden);
	free(roundtrip);
	free(ucs);
	free(input);
}

static void test_unistr_corpus(void)
{
	ntfschar *upcase = NULL;
	u32 upcase_len;
	ntfs_volume vol;

	memset(&vol, 0, sizeof(vol));
	upcase_len = ntfs_upcase_build_default(&upcase);
	TEST_ASSERT(upcase != NULL);
	vol.upcase = upcase;
	vol.upcase_len = upcase_len;
	test_for_each_corpus_file("tests/corpus/unistr", 20,
		test_unistr_corpus_case, &vol);
	free(upcase);
}

int main(void)
{
	(void)setlocale(LC_ALL, "C");
	TEST_RUN(test_name_comparison_and_case_mapping);
	TEST_RUN(test_string_conversion_and_reserved_names);
	TEST_RUN(test_unistr_corpus);
	return 0;
}
