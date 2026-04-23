#include "test_common.h"

#include <strings.h>

#include "bootsect.h"

struct fake_seek_ctx {
	s64 last_offset;
	int fail;
};

static s64 fake_seek(struct ntfs_device *dev, s64 offset, int whence)
{
	struct fake_seek_ctx *ctx = (struct fake_seek_ctx*)dev->d_private;

	TEST_ASSERT_EQ_INT(whence, SEEK_SET);
	ctx->last_offset = offset;
	if (ctx->fail)
		return -1;
	return offset;
}

static struct ntfs_device_operations fake_dops = {
	.seek = fake_seek,
};

static NTFS_BOOT_SECTOR make_valid_boot_sector(void)
{
	NTFS_BOOT_SECTOR bs;

	memset(&bs, 0, sizeof(bs));
	bs.oem_id = magicNTFS;
	bs.bpb.bytes_per_sector = cpu_to_le16(512);
	bs.bpb.sectors_per_cluster = 8;
	bs.number_of_sectors = cpu_to_sle64(32768);
	bs.mft_lcn = cpu_to_sle64(4);
	bs.mftmirr_lcn = cpu_to_sle64(8);
	bs.clusters_per_mft_record = -10;
	bs.clusters_per_index_record = 1;
	bs.volume_serial_number = cpu_to_le64(0x123456789abcdef0ULL);
	bs.end_of_sector_marker = cpu_to_le16(0xaa55);
	return bs;
}

static void test_boot_sector_validation(void)
{
	NTFS_BOOT_SECTOR bs = make_valid_boot_sector();

	TEST_ASSERT(ntfs_boot_sector_is_ntfs(&bs));

	bs.oem_id = 0;
	TEST_ASSERT(!ntfs_boot_sector_is_ntfs(&bs));

	bs = make_valid_boot_sector();
	bs.bpb.bytes_per_sector = cpu_to_le16(128);
	TEST_ASSERT(!ntfs_boot_sector_is_ntfs(&bs));

	bs = make_valid_boot_sector();
	bs.bpb.sectors_per_cluster = 3;
	TEST_ASSERT(!ntfs_boot_sector_is_ntfs(&bs));

	bs = make_valid_boot_sector();
	bs.bpb.reserved_sectors = cpu_to_le16(1);
	TEST_ASSERT(!ntfs_boot_sector_is_ntfs(&bs));

	bs = make_valid_boot_sector();
	bs.clusters_per_mft_record = 3;
	TEST_ASSERT(!ntfs_boot_sector_is_ntfs(&bs));

	bs = make_valid_boot_sector();
	bs.mft_lcn = cpu_to_sle64(1);
	bs.mftmirr_lcn = cpu_to_sle64(1);
	TEST_ASSERT(!ntfs_boot_sector_is_ntfs(&bs));
}

static void test_boot_sector_parse_success(void)
{
	NTFS_BOOT_SECTOR bs = make_valid_boot_sector();
	struct fake_seek_ctx seek_ctx = {0};
	struct ntfs_device dev = {
		.d_ops = &fake_dops,
		.d_private = &seek_ctx,
	};
	ntfs_volume vol;

	memset(&vol, 0, sizeof(vol));
	vol.dev = &dev;

	TEST_ASSERT_EQ_INT(ntfs_boot_sector_parse(&vol, &bs), 0);
	TEST_ASSERT_EQ_LL(vol.vol_serial, 0x123456789abcdef0ULL);
	TEST_ASSERT_EQ_INT(vol.sector_size, 512);
	TEST_ASSERT_EQ_INT(vol.sector_size_bits, 9);
	TEST_ASSERT_EQ_INT(vol.cluster_size, 4096);
	TEST_ASSERT_EQ_INT(vol.cluster_size_bits, 12);
	TEST_ASSERT_EQ_INT(vol.mft_record_size, 1024);
	TEST_ASSERT_EQ_INT(vol.mft_record_size_bits, 10);
	TEST_ASSERT_EQ_INT(vol.indx_record_size, 4096);
	TEST_ASSERT_EQ_INT(vol.indx_record_size_bits, 12);
	TEST_ASSERT_EQ_INT(vol.nr_clusters, 4096);
	TEST_ASSERT_EQ_INT(vol.mftmirr_size, 4);
	TEST_ASSERT_EQ_LL(seek_ctx.last_offset, ((s64)32767) << 9);
}

static void test_boot_sector_parse_failures(void)
{
	NTFS_BOOT_SECTOR bs = make_valid_boot_sector();
	struct fake_seek_ctx seek_ctx = {0};
	struct ntfs_device dev = {
		.d_ops = &fake_dops,
		.d_private = &seek_ctx,
	};
	ntfs_volume vol;

	memset(&vol, 0, sizeof(vol));
	vol.dev = &dev;

	bs.number_of_sectors = 0;
	TEST_ASSERT_EQ_INT(ntfs_boot_sector_parse(&vol, &bs), -1);
	TEST_ASSERT_EQ_INT(errno, EINVAL);

	bs = make_valid_boot_sector();
	seek_ctx.fail = 1;
	TEST_ASSERT_EQ_INT(ntfs_boot_sector_parse(&vol, &bs), -1);
	TEST_ASSERT_EQ_INT(errno, EINVAL);
	seek_ctx.fail = 0;

	bs = make_valid_boot_sector();
	bs.bpb.sectors_per_cluster = 6;
	TEST_ASSERT_EQ_INT(ntfs_boot_sector_parse(&vol, &bs), -1);
	TEST_ASSERT_EQ_INT(errno, EINVAL);

	bs = make_valid_boot_sector();
	bs.mft_lcn = cpu_to_sle64(5000);
	TEST_ASSERT_EQ_INT(ntfs_boot_sector_parse(&vol, &bs), -1);
	TEST_ASSERT_EQ_INT(errno, EINVAL);
}

int main(void)
{
	TEST_RUN(test_boot_sector_validation);
	TEST_RUN(test_boot_sector_parse_success);
	TEST_RUN(test_boot_sector_parse_failures);
	return 0;
}
