/*
 * Server-side file definitions
 *
 * Copyright (C) 2003 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_SERVER_FILE_H
#define __WINE_SERVER_FILE_H

#include <sys/types.h>

#include "object.h"

struct fd;
struct mapping;
struct async_queue;
struct completion;

/* server-side representation of I/O status block */
struct iosb
{
    struct object obj;          /* object header */
    unsigned int  status;       /* resulting status (or STATUS_PENDING) */
    data_size_t   result;       /* size of result (input or output depending on the type) */
    data_size_t   in_size;      /* size of input data */
    void         *in_data;      /* input data */
    data_size_t   out_size;     /* size of output data */
    void         *out_data;     /* output data */
};

/* operations valid on file descriptor objects */
struct fd_ops
{
    /* get the events we want to poll() for on this object */
    int  (*get_poll_events)(struct fd *);
    /* a poll() event occurred */
    void (*poll_event)(struct fd *,int event);
    /* get file information */
    enum server_fd_type (*get_fd_type)(struct fd *fd);
    /* perform a read on the file */
    obj_handle_t (*read)(struct fd *, struct async *, int, file_pos_t );
    /* perform a write on the file */
    obj_handle_t (*write)(struct fd *, struct async *, int, file_pos_t );
    /* flush the object buffers */
    obj_handle_t (*flush)(struct fd *, struct async *, int);
    /* perform an ioctl on the file */
    obj_handle_t (*ioctl)(struct fd *fd, ioctl_code_t code, struct async *async, int blocking );
    /* queue an async operation */
    void (*queue_async)(struct fd *, struct async *async, int type, int count);
    /* selected events for async i/o need an update */
    void (*reselect_async)( struct fd *, struct async_queue *queue );
};

/* file descriptor functions */

extern struct fd *alloc_pseudo_fd( const struct fd_ops *fd_user_ops, struct object *user,
                                   unsigned int options );
extern void set_no_fd_status( struct fd *fd, unsigned int status );
extern struct fd *open_fd( struct fd *root, const char *name, int flags, mode_t *mode,
                           unsigned int access, unsigned int sharing, unsigned int options );
extern struct fd *create_anonymous_fd( const struct fd_ops *fd_user_ops,
                                       int unix_fd, struct object *user, unsigned int options );
extern struct fd *dup_fd_object( struct fd *orig, unsigned int access, unsigned int sharing,
                                 unsigned int options );
extern struct fd *get_fd_object_for_mapping( struct fd *fd, unsigned int access, unsigned int sharing );
extern void *get_fd_user( struct fd *fd );
extern void set_fd_user( struct fd *fd, const struct fd_ops *ops, struct object *user );
extern unsigned int get_fd_options( struct fd *fd );
extern int get_unix_fd( struct fd *fd );
extern int is_same_file_fd( struct fd *fd1, struct fd *fd2 );
extern int is_fd_removable( struct fd *fd );
extern int fd_close_handle( struct object *obj, struct process *process, obj_handle_t handle );
extern int check_fd_events( struct fd *fd, int events );
extern void set_fd_events( struct fd *fd, int events );
extern obj_handle_t lock_fd( struct fd *fd, file_pos_t offset, file_pos_t count, int shared, int wait );
extern void unlock_fd( struct fd *fd, file_pos_t offset, file_pos_t count );
extern void allow_fd_caching( struct fd *fd );
extern void set_fd_signaled( struct fd *fd, int signaled );
extern int is_fd_signaled( struct fd *fd );
extern char *dup_fd_name( struct fd *root, const char *name );

extern int default_fd_signaled( struct object *obj, struct wait_queue_entry *entry );
extern unsigned int default_fd_map_access( struct object *obj, unsigned int access );
extern int default_fd_get_poll_events( struct fd *fd );
extern void default_poll_event( struct fd *fd, int event );
extern int fd_queue_async( struct fd *fd, struct async *async, int type );
extern void fd_async_wake_up( struct fd *fd, int type, unsigned int status );
extern void fd_reselect_async( struct fd *fd, struct async_queue *queue );
extern obj_handle_t no_fd_read( struct fd *fd, struct async *async, int blocking, file_pos_t pos );
extern obj_handle_t no_fd_write( struct fd *fd, struct async *async, int blocking, file_pos_t pos );
extern obj_handle_t no_fd_flush( struct fd *fd, struct async *async, int blocking );
extern obj_handle_t no_fd_ioctl( struct fd *fd, ioctl_code_t code, struct async *async, int blocking );
extern obj_handle_t default_fd_ioctl( struct fd *fd, ioctl_code_t code, struct async *async, int blocking );
extern void no_fd_queue_async( struct fd *fd, struct async *async, int type, int count );
extern void default_fd_queue_async( struct fd *fd, struct async *async, int type, int count );
extern void default_fd_reselect_async( struct fd *fd, struct async_queue *queue );
extern void main_loop(void);
extern void remove_process_locks( struct process *process );

static inline struct fd *get_obj_fd( struct object *obj ) { return obj->ops->get_fd( obj ); }

/* timeout functions */

struct timeout_user;
extern timeout_t current_time;

#define TICKS_PER_SEC 10000000

typedef void (*timeout_callback)( void *private );

extern struct timeout_user *add_timeout_user( timeout_t when, timeout_callback func, void *private );
extern void remove_timeout_user( struct timeout_user *user );
extern const char *get_timeout_str( timeout_t timeout );

/* file functions */

extern struct file *get_file_obj( struct process *process, obj_handle_t handle,
                                  unsigned int access );
extern int get_file_unix_fd( struct file *file );
extern int is_same_file( struct file *file1, struct file *file2 );
extern struct file *create_file_for_fd( int fd, unsigned int access, unsigned int sharing );
extern struct file *create_file_for_fd_obj( struct fd *fd, unsigned int access, unsigned int sharing );
extern void file_set_error(void);
extern struct security_descriptor *mode_to_sd( mode_t mode, const SID *user, const SID *group );
extern mode_t sd_to_mode( const struct security_descriptor *sd, const SID *owner );
extern int set_file_sd( struct object *obj, struct fd *fd, mode_t *mode, uid_t *uid,
                        const struct security_descriptor *sd, unsigned int set_info );
extern struct security_descriptor *get_file_sd( struct object *obj, struct fd *fd, mode_t *mode,
                                                uid_t *uid );

/* file mapping functions */

extern struct mapping *get_mapping_obj( struct process *process, obj_handle_t handle,
                                        unsigned int access );
extern obj_handle_t open_mapping_file( struct process *process, struct mapping *mapping,
                                       unsigned int access, unsigned int sharing );
extern struct mapping *grab_mapping_unless_removable( struct mapping *mapping );
extern int get_page_size(void);

/* device functions */

extern struct object *create_named_pipe_device( struct object *root, const struct unicode_str *name );
extern struct object *create_mailslot_device( struct object *root, const struct unicode_str *name );
extern struct object *create_unix_device( struct object *root, const struct unicode_str *name,
                                          const char *unix_path );

/* shared memory functions */

extern int allocate_shared_memory( int *fd, void **memory, size_t size );
extern void release_shared_memory( int fd, void *memory, size_t size );
extern void init_shared_memory( void );
extern shmglobal_t *shmglobal;
extern int          shmglobal_fd;

/* change notification functions */

extern void do_change_notify( int unix_fd );
extern void sigio_callback(void);
extern struct object *create_dir_obj( struct fd *fd, unsigned int access, mode_t mode,
                                      const struct security_descriptor *sd );
extern struct dir *get_dir_obj( struct process *process, obj_handle_t handle, unsigned int access );

/* completion */

extern struct completion *get_completion_obj( struct process *process, obj_handle_t handle, unsigned int access );
extern void add_completion( struct completion *completion, apc_param_t ckey, apc_param_t cvalue,
                            unsigned int status, apc_param_t information );

/* serial port functions */

extern int is_serial_fd( struct fd *fd );
extern struct object *create_serial( struct fd *fd );

/* async I/O functions */
extern struct async_queue *create_async_queue( struct fd *fd );
extern void free_async_queue( struct async_queue *queue );
extern struct async *create_async( struct thread *thread, const async_data_t *data, struct iosb *iosb );
extern void queue_async( struct async_queue *queue, struct async *async );
extern void async_set_timeout( struct async *async, timeout_t timeout, unsigned int status );
extern void async_set_result( struct object *obj, unsigned int status,
                              apc_param_t total, client_ptr_t apc, client_ptr_t apc_arg );
extern int async_queued( struct async_queue *queue );
extern int async_waiting( struct async_queue *queue );
extern void async_terminate( struct async *async, unsigned int status );
extern void async_wake_up( struct async_queue *queue, unsigned int status );
extern struct completion *fd_get_completion( struct fd *fd, apc_param_t *p_key );
extern void fd_copy_completion( struct fd *src, struct fd *dst );
extern struct iosb *create_iosb( const void *in_data, data_size_t in_size, data_size_t out_size );
extern struct iosb *async_get_iosb( struct async *async );
extern void cancel_process_asyncs( struct process *process );

/* access rights that require Unix read permission */
#define FILE_UNIX_READ_ACCESS (FILE_READ_DATA|FILE_READ_ATTRIBUTES|FILE_READ_EA)

/* access rights that require Unix write permission */
#define FILE_UNIX_WRITE_ACCESS (FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_ATTRIBUTES|FILE_WRITE_EA)

/* magic file access rights for mappings */
#define FILE_MAPPING_IMAGE  0x80000000  /* set for SEC_IMAGE mappings */
#define FILE_MAPPING_WRITE  0x40000000  /* set for writable shared mappings */
#define FILE_MAPPING_ACCESS 0x20000000  /* set for all mappings */

#endif  /* __WINE_SERVER_FILE_H */
