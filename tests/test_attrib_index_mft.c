#include "test_common.h"

#include "attrib.h"
#include "index.h"
#include "mft.h"

s64 ntfs_pread(struct ntfs_device *dev __attribute__((unused)),
		const s64 pos __attribute__((unused)),
		s64 count __attribute__((unused)),
		void *b __attribute__((unused)))
{
	errno = ENOSYS;
	return -1;
}

runlist *ntfs_mapping_pairs_decompress(const ntfs_volume *vol
		__attribute__((unused)), const ATTR_RECORD *attr
		__attribute__((unused)), runlist *old_rl __attribute__((unused)))
{
	errno = ENOSYS;
	return NULL;
}

static void init_attrdef(ATTR_DEF *ad, ATTR_TYPES type, s64 min_size, s64 max_size)
{
	memset(ad, 0, sizeof(*ad));
	ad->type = type;
	ad->min_size = cpu_to_sle64(min_size);
	ad->max_size = cpu_to_sle64(max_size);
}

static ATTR_RECORD *make_resident_attr(u8 *buffer, ATTR_TYPES type,
		const void *value, u32 value_size)
{
	ATTR_RECORD *attr = (ATTR_RECORD*)(void*)buffer;

	memset(buffer, 0, 128);
	attr->type = type;
	attr->length = cpu_to_le32((u32)(offsetof(ATTR_RECORD, resident_end) +
		((value_size + 7U) & ~7U)));
	attr->value_length = cpu_to_le32(value_size);
	attr->value_offset = cpu_to_le16((u16)offsetof(ATTR_RECORD, resident_end));
	memcpy(buffer + offsetof(ATTR_RECORD, resident_end), value, value_size);
	return attr;
}

static void test_attribute_value_helpers(void)
{
	u8 raw_attr[128];
	u8 out[8] = { 0 };
	ntfs_volume vol;
	const char value[] = "DATA";
	ATTR_RECORD *attr = make_resident_attr(raw_attr, AT_DATA, value, 4);

	memset(&vol, 0, sizeof(vol));

	errno = 0;
	TEST_ASSERT_EQ_LL(ntfs_get_attribute_value_length(NULL), 0);
	TEST_ASSERT_EQ_INT(errno, EINVAL);

	TEST_ASSERT_EQ_LL(ntfs_get_attribute_value_length(attr), 4);
	TEST_ASSERT_EQ_LL(ntfs_get_attribute_value(&vol, attr, out), 4);
	TEST_ASSERT_MEMEQ(out, value, 4);

	attr->flags = cpu_to_le16(1);
	errno = 0;
	TEST_ASSERT_EQ_LL(ntfs_get_attribute_value(&vol, attr, out), 0);
	TEST_ASSERT_EQ_INT(errno, EOPNOTSUPP);
	attr->flags = 0;
}

static void test_attribute_sanity_and_size_bounds(void)
{
	ntfs_volume vol;
	ATTR_DEF attrdef[3];
	u8 raw_attr[128];
	u8 file_name_value[sizeof(FILE_NAME_ATTR) + 8];
	ATTR_RECORD *attr;
	FILE_NAME_ATTR *file_name = (FILE_NAME_ATTR*)(void*)file_name_value;
	MFT_REF mref = MK_LE_MREF(5, 1);

	memset(&vol, 0, sizeof(vol));
	init_attrdef(&attrdef[0], AT_VOLUME_NAME, 2, 256);
	init_attrdef(&attrdef[1], AT_DATA, 0, 1024);
	memset(&attrdef[2], 0, sizeof(attrdef[2]));
	vol.attrdef = attrdef;
	vol.attrdef_len = sizeof(attrdef);

	attr = make_resident_attr(raw_attr, AT_DATA, "ABCD", 4);
	TEST_ASSERT_EQ_INT(ntfs_attr_inconsistent(attr, mref), 0);
	TEST_ASSERT_EQ_INT(ntfs_attr_size_bounds_check(&vol, AT_DATA, 512), 0);
	TEST_ASSERT_EQ_INT(ntfs_attr_size_bounds_check(&vol, AT_VOLUME_NAME, 0), 0);

	errno = 0;
	TEST_ASSERT_EQ_INT(ntfs_attr_size_bounds_check(&vol, AT_DATA, 4096), -1);
	TEST_ASSERT_EQ_INT(errno, ERANGE);

	memset(file_name_value, 0, sizeof(file_name_value));
	file_name->file_name_length = 0;
	attr = make_resident_attr(raw_attr, AT_FILE_NAME, file_name_value,
		(u32)offsetof(FILE_NAME_ATTR, file_name));
	errno = 0;
	TEST_ASSERT_EQ_INT(ntfs_attr_inconsistent(attr, mref), -1);
	TEST_ASSERT_EQ_INT(errno, EIO);
}

static void test_index_validators(void)
{
	u8 block_buffer[4096];
	u8 entry_buffer[128];
	INDEX_BLOCK *block = (INDEX_BLOCK*)(void*)block_buffer;
	INDEX_ENTRY *entry = (INDEX_ENTRY*)(void*)entry_buffer;

	memset(block_buffer, 0, sizeof(block_buffer));
	block->magic = magic_INDX;
	block->index_block_vcn = cpu_to_sle64(7);
	block->index.allocated_size =
		cpu_to_le32((u32)(sizeof(block_buffer) - offsetof(INDEX_BLOCK, index)));
	block->index.entries_offset = cpu_to_le32(sizeof(INDEX_HEADER));
	block->index.index_length = cpu_to_le32(sizeof(INDEX_HEADER) + 32);
	TEST_ASSERT_EQ_INT(ntfs_index_block_inconsistent(block, sizeof(block_buffer),
		17, 7), 0);

	block->index.entries_offset = cpu_to_le32(sizeof(INDEX_HEADER) - 1);
	TEST_ASSERT_EQ_INT(ntfs_index_block_inconsistent(block, sizeof(block_buffer),
		17, 7), -1);
	block->index.entries_offset = cpu_to_le32(sizeof(INDEX_HEADER));

	memset(entry_buffer, 0, sizeof(entry_buffer));
	entry->data_offset = cpu_to_le16(20);
	entry->data_length = cpu_to_le16(8);
	entry->length = cpu_to_le16(32);
	entry->key_length = cpu_to_le16(4);
	TEST_ASSERT_EQ_INT(ntfs_index_entry_inconsistent(entry,
		COLLATION_NTOFS_ULONG, 22), 0);

	entry->data_length = cpu_to_le16(20);
	TEST_ASSERT_EQ_INT(ntfs_index_entry_inconsistent(entry,
		COLLATION_NTOFS_ULONG, 22), -1);

	memset(entry_buffer, 0, sizeof(entry_buffer));
	entry->length = cpu_to_le16((u16)(offsetof(INDEX_ENTRY,
		key.file_name.file_name) + 2));
	entry->key_length = cpu_to_le16(2);
	entry->key.file_name.file_name_length = 4;
	TEST_ASSERT_EQ_INT(ntfs_index_entry_inconsistent(entry,
		COLLATION_FILE_NAME, 22), -1);
}

static void test_mft_record_validator(void)
{
	ntfs_volume vol;
	u8 record_buffer[1024];
	MFT_RECORD *record = (MFT_RECORD*)(void*)record_buffer;
	ATTR_RECORD *attr;

	memset(&vol, 0, sizeof(vol));
	vol.mft_record_size = sizeof(record_buffer);

	memset(record_buffer, 0, sizeof(record_buffer));
	record->magic = magic_FILE;
	record->attrs_offset = cpu_to_le16((u16)sizeof(MFT_RECORD));
	record->bytes_allocated = cpu_to_le32((u32)sizeof(record_buffer));
	record->bytes_in_use = cpu_to_le32((u32)(sizeof(MFT_RECORD) + 32 + 4));

	attr = (ATTR_RECORD*)(void*)(record_buffer + sizeof(MFT_RECORD));
	attr->type = AT_DATA;
	attr->length = cpu_to_le32(32);
	attr->value_length = cpu_to_le32(4);
	attr->value_offset = cpu_to_le16((u16)offsetof(ATTR_RECORD, resident_end));
	memcpy((u8*)attr + offsetof(ATTR_RECORD, resident_end), "MFT!", 4);
	*(ATTR_TYPES*)((u8*)attr + le32_to_cpu(attr->length)) = AT_END;

	TEST_ASSERT_EQ_INT(ntfs_mft_record_check(&vol, MK_LE_MREF(9, 1), record), 0);

	record->attrs_offset = cpu_to_le16((u16)(sizeof(MFT_RECORD) + 2));
	errno = 0;
	TEST_ASSERT_EQ_INT(ntfs_mft_record_check(&vol, MK_LE_MREF(9, 1), record), -1);
	TEST_ASSERT_EQ_INT(errno, EIO);
}

static void test_parser_corpus_case(const char *path __attribute__((unused)),
		const char *name, const unsigned char *data, size_t size,
		void *opaque __attribute__((unused)))
{
		if (test_name_has_prefix(name, "attr_ok_")
				|| test_name_has_prefix(name, "attr_bad_")) {
			int expected_ok = test_name_has_prefix(name, "attr_ok_");
			TEST_ASSERT(size >= offsetof(ATTR_RECORD, resident_end));
			TEST_ASSERT_EQ_INT(ntfs_attr_inconsistent(
				(const ATTR_RECORD*)(const void*)data, MK_LE_MREF(77, 1)),
				expected_ok ? 0 : -1);
		return;
	}
	if (test_name_has_prefix(name, "index_block_ok_")
			|| test_name_has_prefix(name, "index_block_bad_")) {
		int expected_ok = test_name_has_prefix(name, "index_block_ok_");
		TEST_ASSERT(size >= sizeof(INDEX_BLOCK));
		TEST_ASSERT_EQ_INT(ntfs_index_block_inconsistent(
			(const INDEX_BLOCK*)(const void*)data, (u32)size, 101, 7),
			expected_ok ? 0 : -1);
		return;
	}
	if (test_name_has_prefix(name, "index_entry_ok_")
			|| test_name_has_prefix(name, "index_entry_bad_")) {
		int expected_ok = test_name_has_prefix(name, "index_entry_ok_");
		TEST_ASSERT(size >= sizeof(INDEX_ENTRY_HEADER));
		TEST_ASSERT_EQ_INT(ntfs_index_entry_inconsistent(
			(const INDEX_ENTRY*)(const void*)data, COLLATION_NTOFS_ULONG, 55),
			expected_ok ? 0 : -1);
		return;
	}
	if (test_name_has_prefix(name, "mft_ok_")
			|| test_name_has_prefix(name, "mft_bad_")) {
		ntfs_volume vol;
		int expected_ok = test_name_has_prefix(name, "mft_ok_");

		memset(&vol, 0, sizeof(vol));
		vol.mft_record_size = (u32)size;
		TEST_ASSERT(size >= sizeof(MFT_RECORD));
		TEST_ASSERT_EQ_INT(ntfs_mft_record_check(&vol, MK_LE_MREF(9, 1),
			(MFT_RECORD*)(const void*)data), expected_ok ? 0 : -1);
		return;
	}
	TEST_ASSERT(!"unexpected corpus file name");
}

static void test_parser_corpus(void)
{
	test_for_each_corpus_file("tests/corpus/attrib_index_mft", 20,
		test_parser_corpus_case, NULL);
}

int main(void)
{
	TEST_RUN(test_attribute_value_helpers);
	TEST_RUN(test_attribute_sanity_and_size_bounds);
	TEST_RUN(test_index_validators);
	TEST_RUN(test_mft_record_validator);
	TEST_RUN(test_parser_corpus);
	return 0;
}
