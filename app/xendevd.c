#include <xdd/bridge.h>
#include <xdd/iface.h>
#include <xdd/vif.h>
#include <xdd/xs_helper.h>

#include <fcntl.h>
#include <libudev.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xenstore.h>


enum operation {
    ONLINE  ,
    OFFLINE ,
};

struct xdd_conf {
    int help;
    int daemonize;
};

#define default_conf { 0, 0 }

static int parse_args (int argc, char** argv, struct xdd_conf* conf)
{
    const char *short_opts = "hD";
    const struct option long_opts[] = {
        { "help"               , no_argument       , NULL , 'h' },
        { "daemon"             , no_argument       , NULL , 'D' },
        { NULL , 0 , NULL , 0 }
    };

    int opt;
    int opt_index;
    int error = 0;

    while (1) {
        opt = getopt_long(argc, argv, short_opts, long_opts, &opt_index);

        if (opt == -1) {
            break;
        }

        switch (opt) {
            case 'h':
                conf->help = 1;
                break;

            case 'D':
                conf->daemonize = 1;
                break;

            default:
                error = 1;
                break;
        }
    }

    while (optind < argc) {
        error = 1;

        printf("%s: invalid argument \'%s\'\n", argv[0], argv[optind]);
        optind++;
    }

    return error;
}

static void print_usage(char* cmd)
{
    printf("Usage: %s [OPTION]...\n", cmd);
    printf("\n");
    printf("Options:\n");
    printf("  -D, --daemon           Run in background\n");
    printf("  -h, --help             Display this help and exit\n");
}


int main(int argc, char** argv)
{
    int fd = -1;
    struct udev* udev = NULL;
    struct udev_monitor* mon = NULL;
    struct udev_device *dev = NULL;

    struct xs_handle *xs = NULL;

    enum operation op;
    const char* vif = NULL;
    const char* bridge = NULL;
    const char* xb_path = NULL;
    const char* action = NULL;

    int err;
    struct xdd_conf conf = default_conf;

    /* Parse arguments */
    err = parse_args(argc, argv, &conf);
    if (err || conf.help) {
        print_usage(argv[0]);
        return err ? 1 : 0;
    }

    if (conf.daemonize) {
        daemon(0, 0);
    }


    /* setup udev */
    udev = udev_new();
    mon = udev_monitor_new_from_netlink(udev, "kernel");

    udev_monitor_filter_add_match_subsystem_devtype(mon, "xen-backend", NULL);

    udev_monitor_enable_receiving(mon);

    fd = udev_monitor_get_fd(mon);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);


    /* setup xenstore */
    xs = xs_open(XS_OPEN_READONLY);


    /*  main loop */
    while (1) {
        dev = udev_monitor_receive_device(mon);

        if (dev) {
            if (strncmp(udev_device_get_sysname(dev), "vif-", 4) != 0) {
                goto out;
            }

            vif = udev_device_get_property_value(dev, "vif");
            xb_path = udev_device_get_property_value(dev, "XENBUS_PATH");
            action = udev_device_get_action(dev);

            if (strcmp(action, "online") == 0) {
                op = ONLINE;
            } else if (strcmp(action, "offline") == 0) {
                op = OFFLINE;
            } else {
                goto out;
            }

            bridge = xs_read_k(xs, xb_path, "bridge");
            if (bridge == NULL) {
                xs_write_k(xs, "Unable to read bridge from xenstore", xb_path, "hotplug-error");
                xs_write_k(xs, "error", xb_path, "hotplug-status");
                goto out;
            }

            switch (op) {
                case ONLINE:
                    vif_hotplug_online(xs, xb_path, bridge, vif);
                    break;
                case OFFLINE:
                    vif_hotplug_offline(xs, xb_path, bridge, vif);
                    break;
            }

out:
            udev_device_unref(dev);
        }
    }
}