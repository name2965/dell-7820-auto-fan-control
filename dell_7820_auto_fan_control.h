#define BUF_MAX_SIZE 64

enum {
    I8K_FAN_CPU_0 = 0,
    I8K_FAN_CPU_1,
    I8K_FAN_FRONT_0,
    I8K_FAN_SYS_0,
    I8K_FAN_SYS_1,
    I8K_FAN_REAR_0,
    I8K_FAN_REAR_1,
};

enum {
    TEMP_CPU_0 = 0,
    TEMP_CPU_1,
};

char *fan_name[] = {
    "CPU 0",
    "CPU 1",
    "FRONT 0",
    "SYS 0",
    "SYS 1",
    "REAR 0",
    "REAR 1",
};

char *fan_status[] = {
    "OFF",
    "LOW",
    "HIGH",
    "TURBO",
};

int i8k_fd = -1;
static int sfd = -1;
static pthread_t sigthr;
static char hwmon_path[BUF_MAX_SIZE];
static int term_alt_active = 0;

void term_enter_alt(void) {
    if (isatty(STDOUT_FILENO)) {
        fputs("\033[?1049h\033[H\033[?25l", stdout);
        fflush(stdout);
        term_alt_active = 1;
    }
}

void term_leave_alt(void) {
    if (term_alt_active) {
        fputs("\033[?25h\033[?1049l", stdout);
        fflush(stdout);
        term_alt_active = 0;
    }
}

static inline void term_clear_home(void) {
    if (isatty(STDOUT_FILENO)) {
        fputs("\033[2J\033[H", stdout);
    }
}

static int read_file_content(char *path, char *buf)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return -1;
    }

    ssize_t n = read(fd, buf, BUF_MAX_SIZE - 1);

    close(fd);

    if (n <= 0) {
        perror("read");
        return -1;
    }
    buf[n] = '\0';
    return n;
}

static int dell_get_bios_version() {
    int args[1];
    int rc;

    rc = ioctl(i8k_fd, I8K_BIOS_VERSION, &args);
    if (rc < 0)
        return rc;

    return args[0];
}

static void dell_get_machine_id(char *machine_id)
{
    char args[16] = {'\0',};
    int rc;

    rc = ioctl(i8k_fd, I8K_MACHINE_ID, &args);
    if (rc >= 0)
        memcpy(machine_id, args, sizeof(machine_id));
}

static int dell_get_fn_status()
{
    int args[1];
    int rc;

    rc = ioctl(i8k_fd, I8K_FN_STATUS, &args);
    if (rc)
        return rc;

    return args[0];
}

static int dell_get_power_status()
{
    int args[1];
    int rc;

    rc = ioctl(i8k_fd, I8K_POWER_STATUS, &args);
    if (rc < 0)
        return rc;

    return args[0];
}

static int dell_get_temp(int cpu_id)
{
    char buf[BUF_MAX_SIZE] = {'\0',};
    char path[128] = {'\0',};

    snprintf(path, sizeof(path), "%stemp%d_input", hwmon_path, cpu_id + 1);

    if (read_file_content(path, buf) < 0)
        return -1;

    return atoi(buf) / 1000;
}

static int dell_get_fan_speed(int fan)
{
    int args[1];
    int rc;

    args[0] = fan;
    rc = ioctl(i8k_fd, I8K_GET_SPEED, &args);
    if (rc < 0)
	    return rc;

    return args[0];
}

static int dell_get_fan_status(int fan)
{
    int args[1];
    int rc;

    args[0] = fan;
    rc = ioctl(i8k_fd, I8K_GET_FAN, &args);
    if (rc < 0)
	    return rc;

    return args[0];
}

static int dell_set_fan_speed(int fan, int speed)
{
    int args[2];
    int rc;

    args[0] = fan;
    args[1] = speed;

    rc = ioctl(i8k_fd, I8K_SET_FAN, &args);
    if (rc < 0)
	    return rc;

    return args[0];
}

static void set_all_fans_low(void) {
    dell_set_fan_speed(I8K_FAN_CPU_0,  I8K_FAN_LOW);
    dell_set_fan_speed(I8K_FAN_CPU_1,  I8K_FAN_LOW);
    dell_set_fan_speed(I8K_FAN_FRONT_0,  I8K_FAN_LOW);
    dell_set_fan_speed(I8K_FAN_SYS_0,  I8K_FAN_LOW);
    dell_set_fan_speed(I8K_FAN_SYS_1,  I8K_FAN_LOW);
    dell_set_fan_speed(I8K_FAN_REAR_0, I8K_FAN_LOW);
    dell_set_fan_speed(I8K_FAN_REAR_1, I8K_FAN_LOW);
}

static void cleanup_then_exit(int exit_normally, int sig) {
    term_leave_alt();
    if (i8k_fd >= 0) {
        set_all_fans_low();
        close(i8k_fd);
        i8k_fd = -1;
    }
    if (sfd >= 0) {
        close(sfd);
        sfd = -1;
    }

    if (exit_normally)
        _exit(0);

    struct sigaction dfl = {0};
    dfl.sa_handler = SIG_DFL; sigemptyset(&dfl.sa_mask);
    sigaction(sig, &dfl, NULL);
    raise(sig);
    _exit(128 + sig);
}

static void *signal_thread(void *arg) {
    (void)arg;
    for (;;) {
        struct signalfd_siginfo info;
        ssize_t n = read(sfd, &info, sizeof(info));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            cleanup_then_exit(1, 0);
        } else if (n == sizeof(info)) {
            int sig = info.ssi_signo;

            switch (sig) {
                case SIGINT:
                case SIGTERM:
                case SIGHUP:
                case SIGQUIT:
                case SIGPIPE:
                case SIGUSR1:
                case SIGUSR2:
                case SIGALRM:
                    cleanup_then_exit(1, sig);
                    break;
                case SIGSEGV:
                case SIGABRT:
                case SIGBUS:
                case SIGFPE:
                case SIGILL:
                    cleanup_then_exit(0, sig);
                    break;
                default:
                    cleanup_then_exit(1, sig);
                    break;
            }
        }
    }
    return NULL;
}

static int install_signalfd(void) {
    sigset_t set;
    sigemptyset(&set);
    int sigs[] = {
        SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE, SIGUSR1, SIGUSR2, SIGALRM,
        SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL
    };
    for (size_t i = 0; i < sizeof(sigs)/sizeof(sigs[0]); i++)
        sigaddset(&set, sigs[i]);

    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0)
        return -1;

    sfd = signalfd(-1, &set, SFD_CLOEXEC);
    if (sfd < 0)
        return -1;

    if (pthread_create(&sigthr, NULL, signal_thread, NULL) != 0)
        return -1;

    return 0;
}

static void atexit_cleanup()
{
    if (i8k_fd >= 0)
        set_all_fans_low();
}

static void atexit_close()
{
    if (i8k_fd >= 0) {
        close(i8k_fd);
        i8k_fd = -1;
    }
}
