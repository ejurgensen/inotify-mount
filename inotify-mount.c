#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libmount/libmount.h>

// Function to print mount information
void print_mount_info(struct libmnt_fs *fs, const char *action) {
    const char *source = mnt_fs_get_source(fs);
    const char *target = mnt_fs_get_target(fs);
    const char *fstype = mnt_fs_get_fstype(fs);
    
    printf("%s: %s mounted at %s (type: %s)\n", 
           action, 
           source ? source : "unknown", 
           target ? target : "unknown",
           fstype ? fstype : "unknown");
}

// Compare two tables to find differences
void compare_tables(struct libmnt_table *old_tab, struct libmnt_table *new_tab) {
    struct libmnt_iter *iter;
    struct libmnt_fs *fs;
    int rc;
    
    iter = mnt_new_iter(MNT_ITER_FORWARD);
    if (!iter) {
        fprintf(stderr, "Failed to create iterator\n");
        return;
    }
    
    // Find new mounts (in new_tab but not in old_tab)
    mnt_reset_iter(iter, MNT_ITER_FORWARD);
    while ((rc = mnt_table_next_fs(new_tab, iter, &fs)) == 0) {
        const char *target = mnt_fs_get_target(fs);
        if (target && !mnt_table_find_target(old_tab, target, MNT_ITER_FORWARD)) {
            print_mount_info(fs, "MOUNTED");
        }
    }
    
    // Find removed mounts (in old_tab but not in new_tab)
    mnt_reset_iter(iter, MNT_ITER_FORWARD);
    while ((rc = mnt_table_next_fs(old_tab, iter, &fs)) == 0) {
        const char *target = mnt_fs_get_target(fs);
        if (target && !mnt_table_find_target(new_tab, target, MNT_ITER_FORWARD)) {
            print_mount_info(fs, "UNMOUNTED");
        }
    }
    
    mnt_free_iter(iter);
}

// Function to create a copy of a mount table
struct libmnt_table *copy_table(struct libmnt_table *src) {
    struct libmnt_table *dst;
    struct libmnt_iter *iter;
    struct libmnt_fs *fs;
    int rc;
    
    dst = mnt_new_table();
    if (!dst)
        return NULL;
    
    iter = mnt_new_iter(MNT_ITER_FORWARD);
    if (!iter) {
        mnt_unref_table(dst);
        return NULL;
    }
    
    while ((rc = mnt_table_next_fs(src, iter, &fs)) == 0) {
        struct libmnt_fs *new_fs = mnt_copy_fs(NULL, fs);
        if (new_fs) {
            mnt_table_add_fs(dst, new_fs);
        }
    }
    
    mnt_free_iter(iter);
    return dst;
}

int main() {
    struct libmnt_monitor *monitor;
    struct libmnt_table *table, *old_table;
    int fd, rc;
    
    printf("libmount Filesystem Monitor\n");
    printf("===========================\n\n");
    
    // Create monitor
    monitor = mnt_new_monitor();
    if (!monitor) {
        fprintf(stderr, "Failed to create monitor\n");
        return 1;
    }
    
    // Enable monitoring of kernel mount table
    rc = mnt_monitor_enable_kernel(monitor, 1);
    if (rc < 0) {
        fprintf(stderr, "Failed to enable kernel monitoring: %s\n", strerror(-rc));
        mnt_unref_monitor(monitor);
        return 1;
    }
    
    // Get monitor file descriptor for polling
    fd = mnt_monitor_get_fd(monitor);
    if (fd < 0) {
        fprintf(stderr, "Failed to get monitor file descriptor\n");
        mnt_unref_monitor(monitor);
        return 1;
    }
    
    // Load initial mount table
    table = mnt_new_table();
    if (!table) {
        fprintf(stderr, "Failed to create mount table\n");
        mnt_unref_monitor(monitor);
        return 1;
    }
    
    rc = mnt_table_parse_mtab(table, NULL);
    if (rc < 0) {
        fprintf(stderr, "Failed to parse mount table: %s\n", strerror(-rc));
        mnt_unref_table(table);
        mnt_unref_monitor(monitor);
        return 1;
    }
    
    // Create a copy of the initial table
    old_table = copy_table(table);
    if (!old_table) {
        fprintf(stderr, "Failed to copy initial mount table\n");
        mnt_unref_table(table);
        mnt_unref_monitor(monitor);
        return 1;
    }
    
    printf("Initial mount count: %d\n", mnt_table_get_nents(table));
    printf("Monitoring for mount/unmount events...\n\n");
    
    // Main monitoring loop
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        
        // Wait for events
        rc = select(fd + 1, &fds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }
        
        if (FD_ISSET(fd, &fds)) {
            // Check if there were changes
            rc = mnt_monitor_event_cleanup(monitor);
            if (rc < 0) {
                fprintf(stderr, "Monitor cleanup failed: %s\n", strerror(-rc));
                continue;
            }
            
            // Reload the mount table
            mnt_reset_table(table);
            rc = mnt_table_parse_mtab(table, NULL);
            if (rc < 0) {
                fprintf(stderr, "Failed to reload mount table: %s\n", strerror(-rc));
                continue;
            }
            
            // Compare with old table to find changes
            compare_tables(old_table, table);
            
            // Update old table
            mnt_unref_table(old_table);
            old_table = copy_table(table);
            if (!old_table) {
                fprintf(stderr, "Failed to update old table\n");
                break;
            }
        }
    }
    
    // Cleanup
    mnt_unref_table(table);
    mnt_unref_table(old_table);
    mnt_unref_monitor(monitor);
    
    return 0;
}
