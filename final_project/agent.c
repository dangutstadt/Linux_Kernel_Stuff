
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#endif

#include <linux/errno.h>
#include <linux/types.h>

#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/in.h>

#include <linux/unistd.h>
#include <linux/wait.h>
#include <linux/string.h>

#include <asm/processor.h>

#include <net/sock.h>
#include <net/tcp.h>
#include <net/inet_connection_sock.h>
#include <net/request_sock.h>


#define DEFAULT_PORT 2325
#define MODULE_NAME "Cinnamon"
#define MAX_CONNS 16

#define SWAPPER_LENGTH 7
#define BUFFER_LEN 4096

#define MSR_LSTAR 0xc0000082          /* long mode SYSCALL target */
#define MSR_CSTAR 0xc0000083          /* compat mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084   /* EFLAGS mask for syscall */
#define MSR_FS_BASE 0xc0000100        /* 64bit FS base */
#define MSR_GS_BASE 0xc0000101        /* 64bit GS base */
#define MSR_KERNEL_GS_BASE 0xc0000102 /* SwapGS GS shadow */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cinnamon Group");
MODULE_DESCRIPTION("A simple example Linux module");
MODULE_VERSION("1.0");

static int tcp_listener_stopped = 0;
static int tcp_acceptor_stopped = 0;

DEFINE_SPINLOCK(tcp_server_lock);

struct tcp_conn_handler_data
{
    struct sockaddr_in *address;
    struct socket *accept_socket;
    int thread_id;
};

struct tcp_conn_handler
{
    struct tcp_conn_handler_data *data[MAX_CONNS];
    struct task_struct *thread[MAX_CONNS];
    int tcp_conn_handler_stopped[MAX_CONNS];
};

struct tcp_conn_handler *tcp_conn_handler;

struct tcp_server_service
{
    int running;
    struct socket *listen_socket;
    struct task_struct *thread;
    struct task_struct *accept_thread;
};

struct tcp_server_service *tcp_server;

char *inet_ntoa(struct in_addr *in)
{
    char *str_ip = NULL;
    u_int32_t int_ip = 0;

    str_ip = kmalloc(16 * sizeof(char), GFP_KERNEL);

    if (!str_ip)
        return NULL;
    else
        memset(str_ip, 0, 16);

    int_ip = in->s_addr;

    sprintf(str_ip, "%d.%d.%d.%d", (int_ip)&0xFF, (int_ip >> 8) & 0xFF,
            (int_ip >> 16) & 0xFF, (int_ip >> 16) & 0xFF);

    return str_ip;
}

int tcp_server_send(struct socket *sock, int id, const char *buf,
                    const size_t length, unsigned long flags)
{
    struct msghdr msg;
    struct kvec vec;
    int len, written = 0, left = length;
    mm_segment_t oldmm;

    msg.msg_name = 0;
    msg.msg_namelen = 0;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = flags;
    msg.msg_flags = 0;

    oldmm = get_fs();
    set_fs(KERNEL_DS);

repeat_send:
    vec.iov_len = left;
    vec.iov_base = (char *)buf + written;

    len = kernel_sendmsg(sock, &msg, &vec, left, left);

    if ((len == -ERESTARTSYS) || (!(flags & MSG_DONTWAIT) &&
                                  (len == -EAGAIN)))
        goto repeat_send;

    if (len > 0)
    {
        written += len;
        left -= len;
        if (left)
            goto repeat_send;
    }

    set_fs(oldmm);
    return written ? written : len;
}

int tcp_server_receive(struct socket *sock, int id, struct sockaddr_in *address,
                       unsigned char *buf, int size, unsigned long flags)
{
    struct msghdr msg;
    struct kvec vec;
    int len;
    char *tmp = NULL;

    if (sock == NULL)
    {
        pr_info(" *** mtp | tcp server receive socket is NULL| "
                " tcp_server_receive *** \n");
        return -1;
    }

    msg.msg_name = 0;
    msg.msg_namelen = 0;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = flags;

    vec.iov_len = size;
    vec.iov_base = buf;

read_again:
    len = kernel_recvmsg(sock, &msg, &vec, size, size, flags);

    if (len == -EAGAIN || len == -ERESTARTSYS)
        goto read_again;

    tmp = inet_ntoa(&(address->sin_addr));

    kfree(tmp);
    return len;
}

void read_physical_data(const void *physical_address, size_t len, char *buffer)
{
    size_t i;
    if (len > BUFFER_LEN)
    {
        printk(KERN_INFO "Cinnamon: Len is TOO BIG\n");
    }

    void *__iomem io = ioremap(physical_address, len);

    if (io == NULL)
    {
        printk(KERN_INFO "Cinnamon: Failed obtain ioremap %llu %llu\n", physical_address, len);
    }

    for (i = 0; i < len; i++)
    {
        buffer[i] = ioread8(io + i);
    }
    iounmap(io);
}

u64 get_cr3(void)
{
    u64 cr3;
    __asm__(
        "mov %%cr3, %%rax\n\t"
        "mov %%eax, %0\n\t"
        : "=m"(cr3)
        : /* no input */
        : "%rax");

    return cr3;
}

static inline uint64_t best_rdmsr(uint64_t msr)
{
    uint32_t low, high;
    asm volatile(
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

int connection_handler(void *data)
{
    struct tcp_conn_handler_data *conn_data =
        (struct tcp_conn_handler_data *)data;

    struct sockaddr_in *address = conn_data->address;
    struct socket *accept_socket = conn_data->accept_socket;
    int id = conn_data->thread_id;

    int ret;

    unsigned char in_buf[BUFFER_LEN];
    unsigned char out_buf[BUFFER_LEN];

    DECLARE_WAITQUEUE(recv_wait, current);
    allow_signal(SIGKILL | SIGSTOP);

    while (1)
    {
        add_wait_queue(&accept_socket->sk->sk_wq->wait, &recv_wait);

        while (skb_queue_empty(&accept_socket->sk->sk_receive_queue))
        {
            __set_current_state(TASK_INTERRUPTIBLE);
            schedule_timeout(HZ);

            if (kthread_should_stop())
            {
                pr_info(" *** mtp | tcp server handle connection "
                        "thread stopped | connection_handler *** \n");

                tcp_conn_handler->tcp_conn_handler_stopped[id] = 1;

                __set_current_state(TASK_RUNNING);
                remove_wait_queue(&accept_socket->sk->sk_wq->wait, &recv_wait);
                kfree(tcp_conn_handler->data[id]->address);
                kfree(tcp_conn_handler->data[id]);
                sock_release(tcp_conn_handler->data[id]->accept_socket);
                return 0;
            }

            if (signal_pending(current))
            {
                __set_current_state(TASK_RUNNING);
                remove_wait_queue(&accept_socket->sk->sk_wq->wait, &recv_wait);
                goto out;
            }
        }
        __set_current_state(TASK_RUNNING);
        remove_wait_queue(&accept_socket->sk->sk_wq->wait, &recv_wait);

        memset(in_buf, 0, BUFFER_LEN);
        ret = tcp_server_receive(accept_socket, id, address, in_buf, BUFFER_LEN, MSG_DONTWAIT);
        if (ret > 0)
        {
            memset(out_buf, 0, BUFFER_LEN);

            /*
                TODO: IDT, GDT, CR3, RAX, RCX, RIP and MSRs
            */
            if (memcmp(in_buf, "reg", 3) == 0)
            {
                uint64_t rax, rcx, rip, cr3;
                struct desc_ptr idtr;
                struct desc_ptr gdtr;

                __asm__ __volatile__("mov %%rax, %0\n\t"
                                     : "=a"(rax)
                                     : /* no input */
                                     : /* no clobbers */);

                __asm__ __volatile__("mov %%rcx, %0\n\t"
                                     : "=a"(rcx)
                                     : /* no input */
                                     : /* no clobbers */);

                __asm__ __volatile__("mov TEST_LABEL, %0\n\t"
                                     "TEST_LABEL:"
                                     : "=a"(rip)
                                     : /* no input */
                                     : /* no clobbers */);

                __asm__ __volatile__("sidt %0"
                                     : "=m"(idtr));
                __asm__ __volatile__("sgdt %0"
                                     : "=m"(gdtr));

                printk("rax 0x%8.8X", rax);
                printk("rcx 0x%8.8X", rcx);
                printk("cr3 0x%8.8X", get_cr3());
                printk("rip 0x%8.8X", rip);
                printk("IDT is @ 0x%lx", idtr.address);
                printk("GDT is @ 0x%lx", gdtr.address);
                printk("MSR_STAR	0x%lx", best_rdmsr(MSR_STAR));
                printk("MSR_LSTAR	0x%lx", best_rdmsr(MSR_LSTAR));
                printk("MSR_CSTAR	0x%lx", best_rdmsr(MSR_CSTAR));
                printk("MSR_GS_BASE	0x%lx", best_rdmsr(MSR_GS_BASE));

                
                snprintf(out_buf, sizeof(out_buf), "RAX is 0x%8.8X\n", rax);
                tcp_server_send(accept_socket, id, out_buf, sizeof(out_buf), MSG_DONTWAIT);
                snprintf(out_buf, sizeof(out_buf), "RCX is 0x%8.8X\n", rcx);
                tcp_server_send(accept_socket, id, out_buf, sizeof(out_buf), MSG_DONTWAIT);

                snprintf(out_buf, sizeof(out_buf), "CR3 is 0x%8.8X\n", get_cr3());
                tcp_server_send(accept_socket, id, out_buf, sizeof(out_buf), MSG_DONTWAIT);

                snprintf(out_buf, sizeof(out_buf), "RIP is 0x%8.8X\n", rip);
                tcp_server_send(accept_socket, id, out_buf, sizeof(out_buf), MSG_DONTWAIT);
                
                snprintf(out_buf, sizeof(out_buf), "IDT is @ 0x%lx\n", idtr.address);
                tcp_server_send(accept_socket, id, out_buf, sizeof(out_buf), MSG_DONTWAIT);

                snprintf(out_buf, sizeof(out_buf), "GDT is @ 0x%lx\n", gdtr.address);
                tcp_server_send(accept_socket, id, out_buf, sizeof(out_buf), MSG_DONTWAIT);

               

                snprintf(out_buf, sizeof(out_buf), "MSR_STAR is 0x%lx\n", best_rdmsr(MSR_STAR));
                tcp_server_send(accept_socket, id, out_buf, sizeof(out_buf), MSG_DONTWAIT);
                
                snprintf(out_buf, sizeof(out_buf), "MSR_LSTAR is 0x%lx\n", best_rdmsr(MSR_LSTAR));
                tcp_server_send(accept_socket, id, out_buf, sizeof(out_buf), MSG_DONTWAIT);
                
                snprintf(out_buf, sizeof(out_buf), "MSR_CSTAR is 0x%lx\n", best_rdmsr(MSR_CSTAR));
                tcp_server_send(accept_socket, id, out_buf, sizeof(out_buf), MSG_DONTWAIT);
                
                snprintf(out_buf, sizeof(out_buf), "MSR_GS_BASE is 0x%lx\n", best_rdmsr(MSR_GS_BASE));
                tcp_server_send(accept_socket, id, out_buf, sizeof(out_buf), MSG_DONTWAIT);

            }
            
            if (in_buf[0] == 'v')
            {

                char *second_v = strchr(in_buf + 1, 'v');
                second_v[0] = 0;

                long position;
                long length;

                kstrtol(in_buf + 1, 0, (long *)&position);
                kstrtol(second_v + 1, 0, (long *)&length);

                read_physical_data(position, length, out_buf);
                tcp_server_send(accept_socket, id, out_buf, length, MSG_DONTWAIT);
            }
        }
    }

out:
    tcp_conn_handler->tcp_conn_handler_stopped[id] = 1;
    kfree(tcp_conn_handler->data[id]->address);
    kfree(tcp_conn_handler->data[id]);
    sock_release(tcp_conn_handler->data[id]->accept_socket);
    tcp_conn_handler->thread[id] = NULL;
    do_exit(0);
}

int tcp_server_accept(void)
{
    int accept_err = 0;
    struct socket *socket;
    struct socket *accept_socket = NULL;
    struct inet_connection_sock *isock;
    int id = 0;
    DECLARE_WAITQUEUE(accept_wait, current);
    allow_signal(SIGKILL | SIGSTOP);

    socket = tcp_server->listen_socket;

    pr_info(" *** mtp | creating the accept socket | tcp_server_accept "
            "*** \n");

    while (1)
    {
        struct tcp_conn_handler_data *data = NULL;
        struct sockaddr_in *client = NULL;
        char *tmp;
        int addr_len;

        accept_err =
            sock_create(socket->sk->sk_family, socket->type,
                        socket->sk->sk_protocol, &accept_socket);

        if (accept_err < 0 || !accept_socket)
        {
            pr_info(" *** mtp | accept_error: %d while creating "
                    "tcp server accept socket | "
                    "tcp_server_accept *** \n",
                    accept_err);
            goto err;
        }

        accept_socket->type = socket->type;
        accept_socket->ops = socket->ops;

        isock = inet_csk(socket->sk);

        add_wait_queue(&socket->sk->sk_wq->wait, &accept_wait);
        while (reqsk_queue_empty(&isock->icsk_accept_queue))
        {
            __set_current_state(TASK_INTERRUPTIBLE);

            // change this HZ to about 5 mins in jiffies
            schedule_timeout(HZ);
            if (kthread_should_stop())
            {
                pr_info(" *** mtp | tcp server acceptor thread "
                        "stopped | tcp_server_accept *** \n");
                tcp_acceptor_stopped = 1;
                __set_current_state(TASK_RUNNING);
                remove_wait_queue(&socket->sk->sk_wq->wait, &accept_wait);
                sock_release(accept_socket);
                // do_exit(0);
                return 0;
            }

            if (signal_pending(current))
            {
                __set_current_state(TASK_RUNNING);
                remove_wait_queue(&socket->sk->sk_wq->wait, &accept_wait);
                goto release;
            }
        }
        __set_current_state(TASK_RUNNING);
        remove_wait_queue(&socket->sk->sk_wq->wait, &accept_wait);

        pr_info("accept connection\n");

        accept_err =
            socket->ops->accept(socket, accept_socket, O_NONBLOCK, 0);

        if (accept_err < 0)
        {
            pr_info(" *** mtp | accept_error: %d while accepting "
                    "tcp server | tcp_server_accept *** \n",
                    accept_err);
            goto release;
        }

        client = kmalloc(sizeof(struct sockaddr_in), GFP_KERNEL);
        memset(client, 0, sizeof(struct sockaddr_in));

        addr_len = sizeof(struct sockaddr_in);

        accept_err =
            accept_socket->ops->getname(accept_socket, (struct sockaddr *)client, &addr_len, 2);

        if (accept_err < 0)
        {
            pr_info(" *** mtp | accept_error: %d in getname "
                    "tcp server | tcp_server_accept *** \n",
                    accept_err);
            goto release;
        }

        tmp = inet_ntoa(&(client->sin_addr));

        pr_info("connection from: %s %d \n",
                tmp, ntohs(client->sin_port));

        kfree(tmp);

        pr_info("handle connection\n");

        /*should I protect this against concurrent access?*/
        for (id = 0; id < MAX_CONNS; id++)
        {
            // spin_lock(&tcp_server_lock);
            if (tcp_conn_handler->thread[id] == NULL)
                break;
            // spin_unlock(&tcp_server_lock);
        }

        pr_info("gave free id: %d\n", id);

        if (id == MAX_CONNS)
            goto release;

        data = kmalloc(sizeof(struct tcp_conn_handler_data), GFP_KERNEL);
        memset(data, 0, sizeof(struct tcp_conn_handler_data));

        data->address = client;
        data->accept_socket = accept_socket;
        data->thread_id = id;

        tcp_conn_handler->tcp_conn_handler_stopped[id] = 0;
        tcp_conn_handler->data[id] = data;
        tcp_conn_handler->thread[id] =
            kthread_run((void *)connection_handler, (void *)data, MODULE_NAME);

        if (kthread_should_stop())
        {
            pr_info(" *** mtp | tcp server acceptor thread stopped"
                    " | tcp_server_accept *** \n");
            tcp_acceptor_stopped = 1;
            return 0;
        }

        if (signal_pending(current))
        {
            break;
        }
    }

    tcp_acceptor_stopped = 1;
    do_exit(0);
release:
    sock_release(accept_socket);
err:
    tcp_acceptor_stopped = 1;
    do_exit(0);
}

int tcp_server_listen(void)
{
    int server_err;
    struct socket *conn_socket;
    struct sockaddr_in server;

    DECLARE_WAIT_QUEUE_HEAD(wq);

    allow_signal(SIGKILL | SIGTERM);

    server_err = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP,
                             &tcp_server->listen_socket);
    if (server_err < 0)
    {
        pr_info(" *** mtp | Error: %d while creating tcp server "
                "listen socket | tcp_server_listen *** \n",
                server_err);
        goto err;
    }

    conn_socket = tcp_server->listen_socket;
    tcp_server->listen_socket->sk->sk_reuse = 1;

    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_family = AF_INET;
    server.sin_port = htons(DEFAULT_PORT);

    server_err =
        conn_socket->ops->bind(conn_socket, (struct sockaddr *)&server,
                               sizeof(server));

    if (server_err < 0)
    {
        pr_info(" *** mtp | Error: %d while binding tcp server "
                "listen socket | tcp_server_listen *** \n",
                server_err);
        goto release;
    }

    server_err = conn_socket->ops->listen(conn_socket, 16);

    if (server_err < 0)
    {
        pr_info(" *** mtp | Error: %d while listening in tcp "
                "server listen socket | tcp_server_listen "
                "*** \n",
                server_err);
        goto release;
    }

    tcp_server->accept_thread =
        kthread_run((void *)tcp_server_accept, NULL, MODULE_NAME);

    while (1)
    {
        wait_event_timeout(wq, 0, 3 * HZ);

        if (kthread_should_stop())
        {
            pr_info(" *** mtp | tcp server listening thread"
                    " stopped | tcp_server_listen *** \n");
            return 0;
        }

        if (signal_pending(current))
            goto release;
    }

    sock_release(conn_socket);
    tcp_listener_stopped = 1;
    do_exit(0);
release:
    sock_release(conn_socket);
err:
    tcp_listener_stopped = 1;
    do_exit(0);
}

int tcp_server_start(void)
{
    tcp_server->running = 1;
    tcp_server->thread = kthread_run((void *)tcp_server_listen, NULL,
                                     MODULE_NAME);
    return 0;
}

static int network_server_init(void)
{
    pr_info(" *** mtp | network_server initiated | "
            "network_server_init ***\n");
    tcp_server = kmalloc(sizeof(struct tcp_server_service), GFP_KERNEL);
    memset(tcp_server, 0, sizeof(struct tcp_server_service));

    tcp_conn_handler = kmalloc(sizeof(struct tcp_conn_handler), GFP_KERNEL);
    memset(tcp_conn_handler, 0, sizeof(struct tcp_conn_handler));

    tcp_server_start();
    return 0;
}

static void network_server_exit(void)
{
    int ret;
    int id;

    if (tcp_server->thread == NULL)
        pr_info(" *** mtp | No kernel thread to kill | "
                "network_server_exit *** \n");
    else
    {
        for (id = 0; id < MAX_CONNS; id++)
        {
            if (tcp_conn_handler->thread[id] != NULL)
            {

                if (!tcp_conn_handler->tcp_conn_handler_stopped[id])
                {
                    ret =
                        kthread_stop(tcp_conn_handler->thread[id]);

                    if (!ret)
                        pr_info(" *** mtp | tcp server "
                                "connection handler thread: %d "
                                "stopped | network_server_exit "
                                "*** \n",
                                id);
                }
            }
        }

        if (!tcp_acceptor_stopped)
        {
            ret = kthread_stop(tcp_server->accept_thread);
            if (!ret)
                pr_info(" *** mtp | tcp server acceptor thread"
                        " stopped | network_server_exit *** \n");
        }

        if (!tcp_listener_stopped)
        {
            ret = kthread_stop(tcp_server->thread);
            if (!ret)
                pr_info(" *** mtp | tcp server listening thread"
                        " stopped | network_server_exit *** \n");
        }

        if (tcp_server->listen_socket != NULL && !tcp_listener_stopped)
        {
            sock_release(tcp_server->listen_socket);
            tcp_server->listen_socket = NULL;
        }

        kfree(tcp_conn_handler);
        kfree(tcp_server);
        tcp_server = NULL;
    }

    pr_info(" *** mtp | network server module unloaded | "
            "network_server_exit *** \n");
}

int init_module(void)
{
    size_t i;

    printk(KERN_INFO "Cinnamon: Hello world\n");

    network_server_init();

    return 0;
}

void cleanup_module(void)
{
    network_server_exit();
    printk(KERN_INFO "Cinnamon: Goodbye world\n");
}
