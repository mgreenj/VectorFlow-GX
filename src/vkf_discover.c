/*
 * Copyright (C) 2026 Maurice Green
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <netpacket/packet.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>

#include "vkf/vkf.h"


#define VFK_FRAME_BUF_LEN   (sizeof(struct ethhdr) + sizeof(vkf_disc_frame_t))

static const uint8_t BCAST_MAC[ETH_ALEN] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};


static void build_disc_frame(const vkf_ctx_t *ctx, vkf_disc_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    memcpy(frame->magic, VFK_DISC_MAGIC, 3);
    frame->version = 1;

    frame->desc.remote_addr  = (uint64_t)(uintptr_t)ctx->gpu_ptr;
    frame->desc.rkey         = ctx->gpu_mr->rkey;
    frame->desc.size         = (uint32_t)ctx->gpu_len;
    frame->desc.node_id      = ctx->node_id;
    frame->desc.buffer_type  = VFK_BUF_TYPE_RX;
    frame->desc.qpn          = ctx->qp->qp_num;

    
    memcpy(frame->desc.gid, ctx->local_gid.raw, 16);
    
    memcpy(frame->desc.mac, ctx->local_mac, ETH_ALEN);
}


int vkf_disc_open_sock(vkf_ctx_t *ctx)
{
    struct sockaddr_ll sll = {0};
    struct ifreq ifr = {0};
    int sock, rc;

    sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock < 0) {
        fprintf(stderr, "vkf_disc_open_sock: socket() failed: %s\n",
                strerror(errno));
        return -errno;
    }

    
    strncpy(ifr.ifr_name, ctx->ifname, IFNAMSIZ - 1);
    rc = ioctl(sock, SIOCGIFINDEX, &ifr);
    if (rc < 0) {
        fprintf(stderr, "vkf_disc_open_sock: SIOCGIFINDEX '%s': %s\n",
                ctx->ifname, strerror(errno));
        close(sock);
        return -errno;
    }
    ctx->ifindex = ifr.ifr_ifindex;

    
    rc = ioctl(sock, SIOCGIFHWADDR, &ifr);
    if (rc < 0) {
        fprintf(stderr, "vkf_disc_open_sock: SIOCGIFHWADDR '%s': %s\n",
                ctx->ifname, strerror(errno));
        close(sock);
        return -errno;
    }
    memcpy(ctx->local_mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

    
    sll.sll_family   = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ALL);
    sll.sll_ifindex  = ctx->ifindex;

    rc = bind(sock, (struct sockaddr *)&sll, sizeof(sll));
    if (rc < 0) {
        fprintf(stderr, "vkf_disc_open_sock: bind() failed: %s\n",
                strerror(errno));
        close(sock);
        return -errno;
    }

    ctx->raw_sock = sock;

    printf("vkf_disc_open_sock: bound to %s (ifindex=%d) "
           "MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           ctx->ifname, ctx->ifindex,
           ctx->local_mac[0], ctx->local_mac[1], ctx->local_mac[2],
           ctx->local_mac[3], ctx->local_mac[4], ctx->local_mac[5]);

    return 0;
}


int vkf_disc_send(vkf_ctx_t *ctx)
{
    uint8_t buf[VFK_FRAME_BUF_LEN];
    struct ethhdr  *eth   = (struct ethhdr *)buf;
    vkf_disc_frame_t *frm = (vkf_disc_frame_t *)(buf + sizeof(struct ethhdr));
    struct sockaddr_ll dst = {0};
    ssize_t sent;

    memset(buf, 0, sizeof(buf));

    
    memcpy(eth->h_dest,   BCAST_MAC,       ETH_ALEN);
    memcpy(eth->h_source, ctx->local_mac,  ETH_ALEN);
    eth->h_proto = htons(VFK_ETHERTYPE);

    
    build_disc_frame(ctx, frm);

    
    dst.sll_family  = AF_PACKET;
    dst.sll_ifindex = ctx->ifindex;
    dst.sll_halen   = ETH_ALEN;
    memcpy(dst.sll_addr, BCAST_MAC, ETH_ALEN);

    sent = sendto(ctx->raw_sock, buf, sizeof(buf), 0,
                  (struct sockaddr *)&dst, sizeof(dst));
    if (sent < 0) {
        fprintf(stderr, "vkf_disc_send: sendto() failed: %s\n",
                strerror(errno));
        return -errno;
    }

    printf("vkf_disc_send: broadcast %zd bytes, node_id=%u QPN=0x%06x\n",
           sent, ctx->node_id, ctx->qp->qp_num);
    return 0;
}


int vkf_disc_recv(vkf_ctx_t *ctx, int timeout_ms)
{
    uint8_t buf[VFK_FRAME_BUF_LEN + 64]; 
    struct ethhdr    *eth;
    vkf_disc_frame_t *frm;
    struct timeval tv;
    fd_set rfds;
    int nb_new = 0;

    struct timespec t_start, t_now;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    for (;;) {
        
        clock_gettime(CLOCK_MONOTONIC, &t_now);
        long elapsed_ms = (t_now.tv_sec  - t_start.tv_sec)  * 1000 +
                          (t_now.tv_nsec - t_start.tv_nsec) / 1000000;
        long remaining  = timeout_ms - elapsed_ms;
        if (remaining <= 0)
            break;

        FD_ZERO(&rfds);
        FD_SET(ctx->raw_sock, &rfds);
        tv.tv_sec  = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        int r = select(ctx->raw_sock + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "vkf_disc_recv: select() error: %s\n",
                    strerror(errno));
            return -errno;
        }
        if (r == 0)
            break; 

        ssize_t n = recv(ctx->raw_sock, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            fprintf(stderr, "vkf_disc_recv: recv() error: %s\n",
                    strerror(errno));
            return -errno;
        }

        if ((size_t)n < sizeof(struct ethhdr) + sizeof(vkf_disc_frame_t))
            continue;

        eth = (struct ethhdr *)buf;
        if (ntohs(eth->h_proto) != VFK_ETHERTYPE)
            continue;

        frm = (vkf_disc_frame_t *)(buf + sizeof(struct ethhdr));

        
        if (memcmp(frm->magic, VFK_DISC_MAGIC, 3) != 0)
            continue;

        
        if (frm->desc.node_id == ctx->node_id)
            continue;

        uint32_t slot = frm->desc.node_id % VFK_MAX_NODES;

        if (ctx->remote_valid[slot] &&
            ctx->remotes[slot].node_id == frm->desc.node_id)
            continue; 

        
        memcpy(&ctx->remotes[slot], &frm->desc, sizeof(vkf_buffer_descriptor_t));
        ctx->remote_valid[slot] = true;
        ctx->nb_remotes++;
        nb_new++;

        printf("vkf_disc_recv: new remote node_id=%u QPN=0x%06x "
               "rkey=0x%08x addr=0x%016llx size=%u\n",
               frm->desc.node_id,
               frm->desc.qpn,
               frm->desc.rkey,
               (unsigned long long)frm->desc.remote_addr,
               frm->desc.size);

       
        if (ctx->qp) {
            int rc;

            rc = vkf_ib_qp_to_rtr(ctx, &ctx->remotes[slot]);
            if (rc) {
                fprintf(stderr, "vkf_disc_recv: qp_to_rtr failed for "
                        "node %u: %d\n", frm->desc.node_id, rc);
                continue;
            }

            rc = vkf_ib_qp_to_rts(ctx);
            if (rc) {
                fprintf(stderr, "vkf_disc_recv: qp_to_rts failed for "
                        "node %u: %d\n", frm->desc.node_id, rc);
                continue;
            }
        }
    }

    return nb_new;
}


void vkf_disc_close_sock(vkf_ctx_t *ctx)
{
    if (ctx->raw_sock >= 0) {
        close(ctx->raw_sock);
        ctx->raw_sock = -1;
    }
}
