#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#define E_USAGE   1
#define E_INVALID 2
#define E_SOCK	  7

#define RTACTION_ADD  1
#define RTACTION_DEL  2

#define GW_FOUND      0
#define GW_NOTFOUND   1
#define GW_ERROR      2

int get_default_gateway_info(
    struct in_addr* gw_addr,
    char *iface_buf,
    size_t iface_buf_len) {
  FILE *fp = fopen("/proc/net/route", "r");
  if (!fp) {
    perror("fopen");
    return GW_ERROR;
  }

  char line[256];
  // Skip header
  if (!fgets(line, sizeof(line), fp)) {
    fclose(fp);
    perror("fgets");
    return GW_ERROR;
  }

  while (fgets(line, sizeof(line), fp)) {
    char iface[IFNAMSIZ];
    unsigned long dest, gateway;
    int flags;

    if (sscanf(line, "%s %lx %lx %x", iface, &dest, &gateway, &flags) != 4) {
      continue;
    }

    // Check if destination is 0.0.0.0 (default route) and interface is up
    if (dest == 0) {
      gw_addr->s_addr = gateway;
      strncpy(iface_buf, iface, iface_buf_len - 1);
      iface_buf[iface_buf_len - 1] = '\0';
      fclose(fp);
      return GW_FOUND;
    }
  }

  fclose(fp);
  return GW_NOTFOUND;
}

void usage(int rc) {
  FILE *fp = rc ? stderr : stdout;
  fprintf(
    fp,
    "Usage: gw [--exec] <ip> [iface]   Update the default gateway\n"
  );
  exit(rc);
}

int edit_default_gateway(
  int exec,
  unsigned long request,
  struct in_addr gw_addr,
  char* iface
) {
  if (request != SIOCADDRT && request != SIOCDELRT) {
	  perror("invalid request");
	  return (E_INVALID);
  }

  struct rtentry rt = {};
  rt.rt_dev = iface;
  rt.rt_flags = RTF_UP | RTF_GATEWAY;

  struct sockaddr_storage sas = {};
  struct sockaddr_in* view_in4 = (struct sockaddr_in*)&sas;
  view_in4->sin_family = AF_INET;
  view_in4->sin_port = 0;

  view_in4->sin_addr.s_addr = INADDR_ANY;
  memcpy(&rt.rt_dst, view_in4, sizeof(rt.rt_dst));

  view_in4->sin_addr = gw_addr;
  memcpy(&rt.rt_gateway, view_in4, sizeof(rt.rt_dst));

  int fd = -1;
  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	  perror("socket");
	  return (E_SOCK);
  }

  if (exec && ioctl(fd, request, &rt) < 0) {
    perror("ioctl");
    close(fd);
    return E_SOCK;
  }
  close(fd);

  return 0;
}

int main(int argc, char* argv[]) {
  int argv_pos = 1;

  if (argv_pos >= argc) {
    usage(E_USAGE);
  }

  int exec = 0;
  if (strcmp(argv[1], "--exec") == 0) {
    printf("Running in EXEC mode.  This requires root priviledge.\n");
    exec = 1;
    ++argv_pos;
  }
  else {
    printf("Running in DRYRUN mode.\n");
  }

  // Get the new gateway address from the argument
  if (argv_pos >= argc) {
    usage(E_USAGE);
  }
  struct in_addr gw_addr_new;
  if (!inet_aton(argv[argv_pos++], &gw_addr_new)) {
    perror("inet_aton");
    return (E_SOCK);
  }

  char* iface = NULL;
  if (argv_pos < argc) {
    iface = argv[argv_pos];
  }

  struct in_addr gw_addr_cur;
  char buf[IFNAMSIZ];
  int result = get_default_gateway_info(&gw_addr_cur, buf, sizeof(buf));
  switch (result) {
    case GW_FOUND:
      if (gw_addr_cur.s_addr == gw_addr_new.s_addr
          && (iface == NULL || strcmp(iface, buf) == 0)) {
        printf("Already set: %08x via %s\n",
          gw_addr_cur.s_addr, buf);
        return 0;
      }

      if (!iface) {
        iface = buf;
      }

      printf("Deleting the default gateway: %08x via %s\n",
        gw_addr_cur.s_addr, iface);
      if ((result = edit_default_gateway(exec, SIOCDELRT, gw_addr_cur, iface))) {
        return result;
      }
      printf("Deleted.\n");
      break;

    case GW_NOTFOUND:
      if (iface) {
        printf("No default gateway is found\n");
      }
      else {
        printf("No default gateway is found.  You need to specify the interface.\n");
        usage(E_USAGE);
      }
      break;

    default:
      return E_SOCK;
  }

  printf("Adding a new default gateway: %08x via %s\n",
    gw_addr_new.s_addr, iface);
  if ((result = edit_default_gateway(exec, SIOCADDRT, gw_addr_new, iface))) {
    return result;
  }
  printf("Added.\n");
  return 0;
}
