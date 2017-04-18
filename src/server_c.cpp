#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <node/uv.h>

uv_loop_t *loop;

uv_buf_t alloc_buffer(uv_handle_t *handle, size_t suggested_size)
{
    return uv_buf_init((char*) malloc(suggested_size), suggested_size);
}

void echo_write(uv_write_t *req, int status)
{
    if (status == -1)
    {
        fprintf(stderr, "Write error %s\n", uv_err_name(uv_last_error(loop)));
    }
    char *base = (char*) req->data;
    free(base);
    free(req);
}

void echo_read(uv_stream_t* client, ssize_t nread, uv_buf_t buf)
{
    if (nread == -1)
    {
        printf("client leave fd\n");
        uv_close((uv_handle_t*) client, NULL);
        return;
    }

    uv_write_t *req = (uv_write_t *) malloc(sizeof(uv_write_t));
    req->data = (void*) buf.base;
    uv_write(req, client, &buf, 1, echo_write);

    uv_read_stop(client);
}

void on_client_leave(uv_handle_t* c)
{
    uv_tcp_t *client = (uv_tcp_t *) c;
    printf("client leave! fd:%d\n", client->io_watcher.fd);
    return;
}

void on_new_connection(uv_stream_t *server, int status)
{
    if (status == -1)
    {
        // error!
        return;
    }

    printf("get new client\n");

    uv_tcp_t *client = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, client);
    printf("accept client %d\n", server->accepted_fd);
    if (uv_accept(server, (uv_stream_t*) client) == 0)
    {
        uv_read_start((uv_stream_t*) client, alloc_buffer, echo_read);
        struct sockaddr_in name;
        int len = sizeof(name);
        char ip[20];
        uv_tcp_getpeername(client, (struct sockaddr *) &name, &len);
        inet_ntop(AF_INET, &name.sin_addr, ip, sizeof(ip));
        printf("come on  %s\n", ip);
    }
    else
    {
        uv_close((uv_handle_t*) client, NULL);
    }
}

int main()
{
    loop = uv_default_loop();

    printf("hehe\n");

    uv_tcp_t server;
    uv_tcp_init(loop, &server);

    struct sockaddr_in bind_addr = uv_ip4_addr("0.0.0.0", 7000);
    uv_tcp_bind(&server, bind_addr);
    int r = uv_listen((uv_stream_t*) &server, 128, on_new_connection);
    if (r)
    {
        fprintf(stderr, "Listen error %s\n", uv_err_name(uv_last_error(loop)));
        return 1;
    }
    return uv_run(loop, UV_RUN_DEFAULT);
}
