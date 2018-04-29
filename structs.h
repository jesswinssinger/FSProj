struct studentfs_dirp {
	DIR *dp;
	struct dirent *entry;
	off_t offset;
};

struct metadata {
	char curr_vnum[MAX_VNUM_LEN];
	size_t vcount;    // # of checkpoints

	/* For configuring checkpointing. -1 indicates indefinite */
	uint32_t vmax;         // Maximum number of checkpoints
	uint32_t size_freq;    // Checkpointing frequency based on size of change
};
