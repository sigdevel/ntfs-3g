#include "test_common.h"

#include "attrib.h"
#include "runlist.h"

s64 ntfs_pread(struct ntfs_device *dev __attribute__((unused)),
		const s64 pos __attribute__((unused)),
		s64 count __attribute__((unused)),
		void *b __attribute__((unused)))
{
	errno = ENOSYS;
	return -1;
}

s64 ntfs_pwrite(struct ntfs_device *dev __attribute__((unused)),
		const s64 pos __attribute__((unused)),
		s64 count __attribute__((unused)),
		const void *b __attribute__((unused)))
{
	errno = ENOSYS;
	return -1;
}

int test_rl_main(int argc, char *argv[]);

static runlist_element *alloc_runlist(size_t count)
{
	size_t bytes = count * sizeof(runlist_element);
	size_t alloc_size = (bytes + 0xfffU) & ~0xfffU;
	runlist_element *rl = calloc(1u, alloc_size ? alloc_size : 0x1000U);
	TEST_ASSERT(rl != NULL);
	return rl;
}

static void assert_run(runlist_element *rl, int index, VCN vcn, LCN lcn, s64 length)
{
	TEST_ASSERT_EQ_LL(rl[index].vcn, vcn);
	TEST_ASSERT_EQ_LL(rl[index].lcn, lcn);
	TEST_ASSERT_EQ_LL(rl[index].length, length);
}

static void test_runlist_merge_and_lookup(void)
{
	runlist_element *dst = alloc_runlist(3);
	runlist_element *src = alloc_runlist(2);
	runlist_element *merged;

	dst[0].vcn = 0;
	dst[0].lcn = LCN_HOLE;
	dst[0].length = 10;
	dst[1].vcn = 10;
	dst[1].lcn = LCN_ENOENT;
	dst[1].length = 0;

	src[0].vcn = 2;
	src[0].lcn = 100;
	src[0].length = 4;
	src[1].vcn = 6;
	src[1].lcn = LCN_RL_NOT_MAPPED;
	src[1].length = 0;

	merged = ntfs_runlists_merge(dst, src);
	TEST_ASSERT(merged != NULL);
	assert_run(merged, 0, 0, LCN_HOLE, 2);
	assert_run(merged, 1, 2, 100, 4);
	assert_run(merged, 2, 6, LCN_HOLE, 4);
	assert_run(merged, 3, 10, LCN_ENOENT, 0);

	TEST_ASSERT_EQ_LL(ntfs_rl_vcn_to_lcn(merged, 1), LCN_HOLE);
	TEST_ASSERT_EQ_LL(ntfs_rl_vcn_to_lcn(merged, 2), 100);
	TEST_ASSERT_EQ_LL(ntfs_rl_vcn_to_lcn(merged, 5), 103);
	TEST_ASSERT_EQ_LL(ntfs_rl_vcn_to_lcn(merged, 10), LCN_ENOENT);

	free(merged);
}

static void test_runlist_mapping_pairs_roundtrip(void)
{
	runlist_element rl[] = {
		{ .vcn = 0, .lcn = 16, .length = 4 },
		{ .vcn = 4, .lcn = 24, .length = 2 },
		{ .vcn = 6, .lcn = LCN_HOLE, .length = 2 },
		{ .vcn = 8, .lcn = 32, .length = 1 },
		{ .vcn = 9, .lcn = LCN_ENOENT, .length = 0 },
	};
	ntfs_volume vol;
	unsigned char buffer[128];
	unsigned char attr_buf[256];
	ATTR_RECORD *attr = (ATTR_RECORD*)(void*)attr_buf;
	runlist_element *decoded;
	int size;

	memset(&vol, 0, sizeof(vol));
	vol.major_ver = 3;
	vol.cluster_size = 4096;
	vol.cluster_size_bits = 12;

	size = ntfs_get_size_for_mapping_pairs(&vol, rl, 0, 128);
	TEST_ASSERT(size > 0);
	TEST_ASSERT_EQ_INT(ntfs_mapping_pairs_build(&vol, buffer, sizeof(buffer), rl, 0, NULL), 0);
	TEST_ASSERT_EQ_INT(buffer[size - 1], 0);

	memset(attr_buf, 0, sizeof(attr_buf));
	attr->type = AT_DATA;
	attr->length = cpu_to_le32((u32)(offsetof(ATTR_RECORD, compressed_end) + size));
	attr->non_resident = 1;
	attr->lowest_vcn = cpu_to_sle64(0);
	attr->highest_vcn = cpu_to_sle64(8);
	attr->mapping_pairs_offset = cpu_to_le16((u16)offsetof(ATTR_RECORD, compressed_end));
	attr->allocated_size = cpu_to_sle64(9LL << vol.cluster_size_bits);
	attr->data_size = attr->allocated_size;
	attr->initialized_size = attr->allocated_size;
	memcpy((unsigned char*)attr + offsetof(ATTR_RECORD, compressed_end), buffer, (size_t)size);

	decoded = ntfs_mapping_pairs_decompress(&vol, attr, NULL);
	TEST_ASSERT(decoded != NULL);
	for (int i = 0; i < 5; i++)
		assert_run(decoded, i, rl[i].vcn, rl[i].lcn, rl[i].length);
	free(decoded);
}

static void test_runlist_helpers(void)
{
	runlist_element rl[] = {
		{ .vcn = 0, .lcn = 40, .length = 3 },
		{ .vcn = 3, .lcn = LCN_HOLE, .length = 2 },
		{ .vcn = 5, .lcn = 50, .length = 4 },
		{ .vcn = 9, .lcn = LCN_ENOENT, .length = 0 },
	};
	ntfs_volume vol;
	u8 bytes[8];
	runlist *mutable_rl;
	runlist_element bad[] = {
		{ .vcn = 0, .lcn = LCN_RL_NOT_MAPPED, .length = 1 },
		{ .vcn = 1, .lcn = LCN_ENOENT, .length = 0 },
	};

	memset(&vol, 0, sizeof(vol));
	vol.major_ver = 3;
	vol.cluster_size_bits = 12;

	TEST_ASSERT_EQ_INT(ntfs_get_nr_significant_bytes(0), 1);
	TEST_ASSERT_EQ_INT(ntfs_get_nr_significant_bytes(127), 1);
	TEST_ASSERT_EQ_INT(ntfs_get_nr_significant_bytes(128), 2);
	TEST_ASSERT_EQ_INT(ntfs_get_nr_significant_bytes(-129), 2);

	TEST_ASSERT_EQ_INT(ntfs_write_significant_bytes(bytes, bytes + 7, 0x1234), 2);
	TEST_ASSERT_EQ_INT(bytes[0], 0x34);
	TEST_ASSERT_EQ_INT(bytes[1], 0x12);
	errno = 0;
	TEST_ASSERT_EQ_INT(ntfs_write_significant_bytes(bytes, bytes, 0x1234), -1);
	TEST_ASSERT_EQ_INT(errno, ENOSPC);

	TEST_ASSERT_EQ_INT(ntfs_rl_sparse(rl), 1);
	TEST_ASSERT_EQ_LL(ntfs_rl_get_compressed_size(&vol, rl), 7LL << 12);
	errno = 0;
	TEST_ASSERT_EQ_INT(ntfs_rl_sparse(bad), -1);
	TEST_ASSERT_EQ_INT(errno, EINVAL);

	mutable_rl = alloc_runlist(4);
	memcpy(mutable_rl, rl, sizeof(rl));
	TEST_ASSERT_EQ_INT(ntfs_rl_truncate(&mutable_rl, 6), 0);
	assert_run(mutable_rl, 0, 0, 40, 3);
	assert_run(mutable_rl, 1, 3, LCN_HOLE, 2);
	assert_run(mutable_rl, 2, 5, 50, 1);
	assert_run(mutable_rl, 3, 6, LCN_ENOENT, 0);
	free(mutable_rl);
}

static void test_runlist_builtin_coverage_modes(void)
{
	char *zero_args[] = { "rl", "zero", NULL };
	char *pure_single[] = { "rl", "pure", "contig", "single", NULL };
	char *pure_multi[] = { "rl", "pure", "contig", "multi", NULL };
	char *noncontig_single[] = { "rl", "pure", "noncontig", "single", NULL };
	char *noncontig_multi[] = { "rl", "pure", "noncontig", "multi", NULL };

	TEST_ASSERT_EQ_INT(test_rl_main(2, zero_args), 0);
	TEST_ASSERT_EQ_INT(test_rl_main(4, pure_single), 0);
	TEST_ASSERT_EQ_INT(test_rl_main(4, pure_multi), 0);
	TEST_ASSERT_EQ_INT(test_rl_main(4, noncontig_single), 0);
	TEST_ASSERT_EQ_INT(test_rl_main(4, noncontig_multi), 0);
}

int main(void)
{
	TEST_RUN(test_runlist_merge_and_lookup);
	TEST_RUN(test_runlist_mapping_pairs_roundtrip);
	TEST_RUN(test_runlist_helpers);
	TEST_RUN(test_runlist_builtin_coverage_modes);
	return 0;
}
