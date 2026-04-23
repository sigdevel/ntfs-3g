#include "test_common.h"

#include <stddef.h>

#include "mst.h"

struct test_record {
	NTFS_RECORD header;
	le16 usa[3];
	unsigned char payload[1024 - sizeof(NTFS_RECORD) - sizeof(le16) * 3];
};

static void init_record(struct test_record *record)
{
	memset(record, 0, sizeof(*record));
	record->header.magic = magic_FILE;
	record->header.usa_ofs = cpu_to_le16((u16)offsetof(struct test_record, usa));
	record->header.usa_count = cpu_to_le16(3);
	record->usa[0] = cpu_to_le16(1);
	*(le16*)((unsigned char*)record + 510) = cpu_to_le16(0xaaaa);
	*(le16*)((unsigned char*)record + 1022) = cpu_to_le16(0xbbbb);
}

static void test_pre_and_post_write_fixup(void)
{
	struct test_record record;

	init_record(&record);
	TEST_ASSERT_EQ_INT(ntfs_mst_pre_write_fixup(&record.header, sizeof(record)), 0);
	TEST_ASSERT_EQ_INT(le16_to_cpu(record.usa[0]), 2);
	TEST_ASSERT_EQ_INT(le16_to_cpu(record.usa[1]), 0xaaaa);
	TEST_ASSERT_EQ_INT(le16_to_cpu(record.usa[2]), 0xbbbb);
	TEST_ASSERT_EQ_INT(le16_to_cpu(*(le16*)((unsigned char*)&record + 510)), 2);
	TEST_ASSERT_EQ_INT(le16_to_cpu(*(le16*)((unsigned char*)&record + 1022)), 2);

	ntfs_mst_post_write_fixup(&record.header);
	TEST_ASSERT_EQ_INT(le16_to_cpu(*(le16*)((unsigned char*)&record + 510)), 0xaaaa);
	TEST_ASSERT_EQ_INT(le16_to_cpu(*(le16*)((unsigned char*)&record + 1022)), 0xbbbb);
}

static void test_post_read_fixup_success(void)
{
	struct test_record record;

	init_record(&record);
	TEST_ASSERT_EQ_INT(ntfs_mst_pre_write_fixup(&record.header, sizeof(record)), 0);
	TEST_ASSERT_EQ_INT(ntfs_mst_post_read_fixup_warn(&record.header, sizeof(record), FALSE), 0);
	TEST_ASSERT_EQ_INT(le16_to_cpu(*(le16*)((unsigned char*)&record + 510)), 0xaaaa);
	TEST_ASSERT_EQ_INT(le16_to_cpu(*(le16*)((unsigned char*)&record + 1022)), 0xbbbb);
}

static void test_post_read_fixup_detects_corruption(void)
{
	struct test_record record;

	init_record(&record);
	TEST_ASSERT_EQ_INT(ntfs_mst_pre_write_fixup(&record.header, sizeof(record)), 0);
	*(le16*)((unsigned char*)&record + 510) = cpu_to_le16(0x1234);
	errno = 0;
	TEST_ASSERT_EQ_INT(ntfs_mst_post_read_fixup_warn(&record.header, sizeof(record), FALSE), -1);
	TEST_ASSERT_EQ_INT(errno, EIO);
	TEST_ASSERT_EQ_INT(record.header.magic, magic_BAAD);
}

static void test_invalid_records_fail(void)
{
	struct test_record record;

	init_record(&record);
	errno = 0;
	TEST_ASSERT_EQ_INT(ntfs_mst_pre_write_fixup(&record.header, 768), -1);
	TEST_ASSERT_EQ_INT(errno, EINVAL);

	record.header.magic = magic_BAAD;
	errno = 0;
	TEST_ASSERT_EQ_INT(ntfs_mst_pre_write_fixup(&record.header, sizeof(record)), -1);
	TEST_ASSERT_EQ_INT(errno, EINVAL);

	init_record(&record);
	record.header.usa_count = cpu_to_le16(2);
	errno = 0;
	TEST_ASSERT_EQ_INT(ntfs_mst_post_read_fixup_warn(&record.header, sizeof(record), FALSE), -1);
	TEST_ASSERT_EQ_INT(errno, EINVAL);
}

int main(void)
{
	TEST_RUN(test_pre_and_post_write_fixup);
	TEST_RUN(test_post_read_fixup_success);
	TEST_RUN(test_post_read_fixup_detects_corruption);
	TEST_RUN(test_invalid_records_fail);
	return 0;
}
