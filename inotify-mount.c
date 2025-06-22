#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#ifdef __linux__
#include <sys/inotify.h>
#include <mntent.h>
#elif defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <fcntl.h>
#endif

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

#ifdef __linux__
void print_mounts() {
    FILE *fp;
    struct mntent *mnt;
    
    fp = setmntent("/proc/mounts", "r");
    if (fp == NULL) {
        perror("setmntent");
        return;
    }
    
    printf("Current mounts:\n");
    while ((mnt = getmntent(fp)) != NULL) {
        printf("  %s on %s type %s\n", mnt->mnt_fsname, mnt->mnt_dir, mnt->mnt_type);
    }
    printf("\n");
    
    endmntent(fp);
}

int monitor_mounts_linux() {
    int fd, wd;
    char buffer[EVENT_BUF_LEN];
    
    fd = inotify_init();
    if (fd < 0) {
        perror("inotify_init");
        return -1;
    }
    
    wd = inotify_add_watch(fd, "/proc/mounts", IN_MODIFY);
    if (wd < 0) {
        perror("inotify_add_watch");
        close(fd);
        return -1;
    }
    
    printf("Monitoring filesystem mounts on Linux...\n");
    print_mounts();
    
    while (1) {
        int length = read(fd, buffer, EVENT_BUF_LEN);
        if (length < 0) {
            perror("read");
            break;
        }
        
        int i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *)&buffer[i];
            
            if (event->mask & IN_MODIFY) {
                printf("Mount table changed!\n");
                print_mounts();
            }
            
            i += EVENT_SIZE + event->len;
        }
    }
    
    inotify_rm_watch(fd, wd);
    close(fd);
    return 0;
}
#endif

#ifdef __FreeBSD__
void print_mounts_freebsd() {
    struct statfs *mounts;
    int count, i;
    
    count = getmntinfo(&mounts, MNT_NOWAIT);
    if (count == 0) {
        perror("getmntinfo");
        return;
    }
    
    printf("Current mounts:\n");
    for (i = 0; i < count; i++) {
        printf("  %s on %s type %s\n", 
               mounts[i].f_mntfromname, 
               mounts[i].f_mntonname, 
               mounts[i].f_fstypename);
    }
    printf("\n");
}

int monitor_mounts_freebsd() {
    int kq, fd;
    struct kevent change;
    struct kevent event;
    struct timespec timeout;
    
    kq = kqueue();
    if (kq == -1) {
        perror("kqueue");
        return -1;
    }
    
    // Monitor /dev for device changes (mount/unmount events)
    fd = open("/dev", O_RDONLY);
    if (fd == -1) {
        perror("open /dev");
        close(kq);
        return -1;
    }
    
    EV_SET(&change, fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_ONESHOT,
           NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_LINK | NOTE_RENAME | NOTE_REVOKE,
           0, 0);
    
    if (kevent(kq, &change, 1, NULL, 0, NULL) == -1) {
        perror("kevent register");
        close(fd);
        close(kq);
        return -1;
    }
    
    printf("Monitoring filesystem mounts on FreeBSD...\n");
    print_mounts_freebsd();
    
    timeout.tv_sec = 1;
    timeout.tv_nsec = 0;
    
    while (1) {
        int nev = kevent(kq, NULL, 0, &event, 1, &timeout);
        
        if (nev == -1) {
            perror("kevent wait");
            break;
        } else if (nev > 0) {
            if (event.filter == EVFILT_VNODE) {
                printf("Filesystem event detected!\n");
                print_mounts_freebsd();
                
                // Re-register the event (ONESHOT means it's automatically removed)
                EV_SET(&change, fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_ONESHOT,
                       NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_LINK | NOTE_RENAME | NOTE_REVOKE,
                       0, 0);
                
                if (kevent(kq, &change, 1, NULL, 0, NULL) == -1) {
                    perror("kevent re-register");
                    break;
                }
            }
        }
        // If nev == 0, it's a timeout, continue monitoring
    }
    
    close(fd);
    close(kq);
    return 0;
}
#endif

int main() {
    printf("Filesystem Mount Detector\n");
    printf("========================\n\n");
    
#ifdef __linux__
    printf("Running on Linux\n\n");
    return monitor_mounts_linux();
#elif defined(__FreeBSD__)
    printf("Running on FreeBSD\n\n");
    return monitor_mounts_freebsd();
#else
    printf("Unsupported operating system\n");
    printf("This program supports Linux and FreeBSD only.\n");
    return 1;
#endif
}
