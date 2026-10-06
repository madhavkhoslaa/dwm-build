/* Minimal X11 status generator for dwm. */
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>

/* Appearance: change these without touching module code. */
#define SEPARATOR " | "
#define ICON_VOLUME "󰕾"
#define ICON_MUTED ""
#define ICON_BRIGHTNESS "󰃠"
#define ICON_WIFI ""
#define ICON_BLUETOOTH ""
#define ICON_CPU ""
#define ICON_RAM "󰍛"
#define ICON_DISK "󰋊"
#define ICON_BATTERY "󰁹"
#define ICON_CHARGING ""
#define ICON_TIME "󰥔"
#define COLOR_WHITE theme_white
#define COLOR_GREY theme_grey
#define COLOR_BLACK theme_black
#define COLOR_GREEN theme_green
#define COLOR_ORANGE theme_orange
#define COLOR_RED theme_red

#define STATUS_SIZE 256
#define MODULE_SIZE 64
#define COMMAND_SIZE 512
#define READ_SIZE 4096

enum audio_backend {
	AUDIO_NONE,
	AUDIO_WPCTL,
	AUDIO_PACTL,
	AUDIO_AMIXER
};

enum refresh_flag {
	REFRESH_VOLUME = 1,
	REFRESH_BRIGHTNESS = 2,
	REFRESH_THEME = 4,
	REFRESH_NETWORK = 8
};

static Display *dpy;
static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t refresh_flags = REFRESH_VOLUME | REFRESH_BRIGHTNESS;
static enum audio_backend audio = AUDIO_NONE;

static int stat_fd = -1;
static int meminfo_fd = -1;
static int wireless_fd = -1;
static int brightness_fd = -1;
static int max_brightness_fd = -1;
static int battery_capacity_fd = -1;
static int battery_status_fd = -1;
static int wifi_state_fd = -1;
static int wifi_flags_fd = -1;

static char wifi_interface[32];
static char command_output[COMMAND_SIZE];
static char command_drain[128];
static char read_buffer[READ_SIZE];
static char command[128];
static char theme_white[8] = "#ffffff";
static char theme_grey[8] = "#777777";
static char theme_black[8] = "#000000";
static char theme_green[8] = "#00cc66";
static char theme_orange[8] = "#ff9900";
static char theme_red[8] = "#ff3333";

static char volume_text[MODULE_SIZE];
static char brightness_text[MODULE_SIZE];
static char wifi_text[MODULE_SIZE];
static char bluetooth_text[MODULE_SIZE];
static char cpu_text[MODULE_SIZE];
static char ram_text[MODULE_SIZE];
static char disk_text[MODULE_SIZE];
static char battery_text[MODULE_SIZE];
static char time_text[MODULE_SIZE];

static int read_fd(int fd, char *buffer, size_t size);

static void
handle_signal(int signo)
{
	if (signo == SIGINT || signo == SIGTERM)
		running = 0;
	else if (signo == SIGRTMIN + 1)
		refresh_flags |= REFRESH_VOLUME;
	else if (signo == SIGRTMIN + 2)
		refresh_flags |= REFRESH_BRIGHTNESS;
	else if (signo == SIGRTMIN + 3)
		refresh_flags |= REFRESH_NETWORK;
	else if (signo == SIGHUP)
		refresh_flags |= REFRESH_THEME;
}

static int
copy_palette_color(char destination[8], const char *palette, int index)
{
	const char *line = palette;
	int current;

	for (current = 0; current < index; current++) {
		line = strchr(line, '\n');
		if (!line)
			return -1;
		line++;
	}
	if (line[0] != '#' || strlen(line) < 7)
		return -1;
	memcpy(destination, line, 7);
	destination[7] = '\0';
	return 0;
}

static void
load_theme_colors(void)
{
	char path[256];
	const char *home;
	int fd;

	home = getenv("HOME");
	if (!home || snprintf(path, sizeof(path), "%s/.cache/wal/colors", home) >=
	             (int)sizeof(path))
		return;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return;
	if (read_fd(fd, read_buffer, sizeof(read_buffer)) > 0) {
		(void)copy_palette_color(theme_black, read_buffer, 0);
		(void)copy_palette_color(theme_red, read_buffer, 1);
		(void)copy_palette_color(theme_green, read_buffer, 2);
		(void)copy_palette_color(theme_orange, read_buffer, 3);
		(void)copy_palette_color(theme_white, read_buffer, 7);
		(void)copy_palette_color(theme_grey, read_buffer, 8);
	}
	close(fd);
}

static int
read_fd(int fd, char *buffer, size_t size)
{
	ssize_t count;

	if (fd < 0 || size < 2)
		return -1;
	count = pread(fd, buffer, size - 1, 0);
	if (count <= 0)
		return -1;
	buffer[count] = '\0';
	return (int)count;
}

static int
read_long_fd(int fd, long *value)
{
	char buffer[32];
	char *end;
	long parsed;

	if (read_fd(fd, buffer, sizeof(buffer)) < 0)
		return -1;
	errno = 0;
	parsed = strtol(buffer, &end, 10);
	if (errno != 0 || end == buffer)
		return -1;
	*value = parsed;
	return 0;
}

static int
read_hex_fd(int fd, unsigned long *value)
{
	char buffer[32];
	char *end;
	unsigned long parsed;

	if (read_fd(fd, buffer, sizeof(buffer)) < 0)
		return -1;
	errno = 0;
	parsed = strtoul(buffer, &end, 0);
	if (errno != 0 || end == buffer)
		return -1;
	*value = parsed;
	return 0;
}

static int
command_exists(const char *name)
{
	const char *path, *end;
	size_t directory_length, name_length;
	char candidate[256];

	path = getenv("PATH");
	if (!path)
		return 0;
	name_length = strlen(name);
	while (*path) {
		end = strchr(path, ':');
		directory_length = end ? (size_t)(end - path) : strlen(path);
		if (directory_length + name_length + 2 <= sizeof(candidate)) {
			if (directory_length == 0)
				directory_length = (size_t)snprintf(candidate, sizeof(candidate), ".");
			else {
				memcpy(candidate, path, directory_length);
				candidate[directory_length] = '\0';
			}
			candidate[directory_length] = '/';
			memcpy(candidate + directory_length + 1, name, name_length + 1);
			if (access(candidate, X_OK) == 0)
				return 1;
		}
		if (!end)
			break;
		path = end + 1;
	}
	return 0;
}

static const char *
run_command(const char *cmd)
{
	FILE *pipe;
	size_t used, count;

	command_output[0] = '\0';
	pipe = popen(cmd, "r");
	if (!pipe)
		return NULL;
	used = 0;
	while (used < sizeof(command_output) - 1) {
		count = fread(command_output + used, 1,
		              sizeof(command_output) - 1 - used, pipe);
		used += count;
		if (count == 0)
			break;
	}
	command_output[used] = '\0';
	while (fread(command_drain, 1, sizeof(command_drain), pipe) > 0)
		;
	if (pclose(pipe) == -1 || used == 0)
		return NULL;
	return command_output;
}

static int
open_first_class_device(const char *directory, const char *prefix,
                        const char *file, char *path, size_t path_size)
{
	DIR *dir;
	struct dirent *entry;
	int fd = -1;

	dir = opendir(directory);
	if (!dir)
		return -1;
	while ((entry = readdir(dir))) {
		if (entry->d_name[0] == '.' ||
		    (prefix && strncmp(entry->d_name, prefix, strlen(prefix)) != 0))
			continue;
		if (snprintf(path, path_size, "%s/%s/%s", directory,
		             entry->d_name, file) >= (int)path_size)
			continue;
		fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd >= 0)
			break;
	}
	closedir(dir);
	return fd;
}

static void
discover_brightness(void)
{
	char path[256];
	char *slash;

	brightness_fd = open_first_class_device("/sys/class/backlight", NULL,
	                                        "brightness", path, sizeof(path));
	if (brightness_fd < 0)
		return;
	slash = strrchr(path, '/');
	if (!slash)
		return;
	(void)snprintf(slash + 1, (size_t)(path + sizeof(path) - slash - 1),
	               "max_brightness");
	max_brightness_fd = open(path, O_RDONLY | O_CLOEXEC);
}

static void
discover_battery(void)
{
	char path[256];
	char *slash;

	battery_capacity_fd = open_first_class_device("/sys/class/power_supply",
	                                              "BAT", "capacity", path,
	                                              sizeof(path));
	if (battery_capacity_fd < 0)
		return;
	slash = strrchr(path, '/');
	if (!slash)
		return;
	(void)snprintf(slash + 1, (size_t)(path + sizeof(path) - slash - 1),
	               "status");
	battery_status_fd = open(path, O_RDONLY | O_CLOEXEC);
}

static void
discover_wifi(void)
{
	DIR *dir;
	struct dirent *entry;
	char path[256];
	struct stat st;

	dir = opendir("/sys/class/net");
	if (!dir)
		return;
	while ((entry = readdir(dir))) {
		if (entry->d_name[0] == '.')
			continue;
		if (snprintf(path, sizeof(path), "/sys/class/net/%s/wireless",
		             entry->d_name) >= (int)sizeof(path))
			continue;
		if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
			if (strlen(entry->d_name) >= sizeof(wifi_interface))
				continue;
			memcpy(wifi_interface, entry->d_name,
			       strlen(entry->d_name) + 1);
			break;
		}
	}
	closedir(dir);
	if (!wifi_interface[0])
		return;
	(void)snprintf(path, sizeof(path), "/sys/class/net/%s/operstate",
	               wifi_interface);
	wifi_state_fd = open(path, O_RDONLY | O_CLOEXEC);
	(void)snprintf(path, sizeof(path), "/sys/class/net/%s/flags",
	               wifi_interface);
	wifi_flags_fd = open(path, O_RDONLY | O_CLOEXEC);
}

static void
refresh_bluetooth(void)
{
	const char *output;

	bluetooth_text[0] = '\0';
	output = run_command("bluetoothctl show 2>/dev/null");
	if (!output || !output[0])
		return;
	if (strstr(output, "Powered: yes"))
		(void)snprintf(bluetooth_text, sizeof(bluetooth_text),
		               "^c%s^%s^d^", COLOR_GREEN, ICON_BLUETOOTH);
	else
		(void)snprintf(bluetooth_text, sizeof(bluetooth_text),
		               "^c%s^%s off^d^", COLOR_RED, ICON_BLUETOOTH);
}

static void
detect_audio(void)
{
	if (command_exists("wpctl"))
		audio = AUDIO_WPCTL;
	else if (command_exists("pactl"))
		audio = AUDIO_PACTL;
	else if (command_exists("amixer"))
		audio = AUDIO_AMIXER;
}

static void
refresh_volume(void)
{
	const char *output, *percent_sign, *cursor;
	double level;
	long percent = -1;
	int muted = 0;

	volume_text[0] = '\0';
	if (audio == AUDIO_WPCTL) {
		output = run_command("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null");
		if (output && sscanf(output, "Volume: %lf", &level) == 1) {
			percent = (long)(level * 100.0 + 0.5);
			muted = strstr(output, "[MUTED]") != NULL;
		}
	} else if (audio == AUDIO_PACTL) {
		output = run_command("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null; "
		                     "pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null");
		if (output) {
			percent_sign = strchr(output, '%');
			if (percent_sign) {
				cursor = percent_sign;
				while (cursor > output && cursor[-1] >= '0' && cursor[-1] <= '9')
					cursor--;
				percent = strtol(cursor, NULL, 10);
			}
			muted = strstr(output, "Mute: yes") != NULL;
		}
	} else if (audio == AUDIO_AMIXER) {
		output = run_command("amixer get Master 2>/dev/null");
		if (output) {
			percent_sign = strrchr(output, '%');
			if (percent_sign) {
				cursor = percent_sign;
				while (cursor > output && cursor[-1] >= '0' && cursor[-1] <= '9')
					cursor--;
				percent = strtol(cursor, NULL, 10);
			}
			muted = strstr(output, "[off]") != NULL;
		}
	}
	if (percent < 0)
		return;
	(void)snprintf(volume_text, sizeof(volume_text), "%s %ld%%%s",
	               muted ? ICON_MUTED : ICON_VOLUME, percent,
	               muted ? " muted" : "");
}

static void
refresh_brightness(void)
{
	long current, maximum;

	brightness_text[0] = '\0';
	if (read_long_fd(brightness_fd, &current) < 0 ||
	    read_long_fd(max_brightness_fd, &maximum) < 0 || maximum <= 0)
		return;
	(void)snprintf(brightness_text, sizeof(brightness_text), "%s %ld%%",
	               ICON_BRIGHTNESS, current * 100 / maximum);
}

static void
refresh_wifi(void)
{
	char state[16];
	char *line, *colon;
	double quality = -1.0;
	long percent;
	unsigned long flags;
	const char *output;

	wifi_text[0] = '\0';
	if (!wifi_interface[0] || read_fd(wifi_state_fd, state, sizeof(state)) < 0 ||
	    read_hex_fd(wifi_flags_fd, &flags) < 0)
		return;
	if (!(flags & IFF_UP)) {
		(void)snprintf(wifi_text, sizeof(wifi_text), "^c%s^%s off^d^",
		               COLOR_RED, ICON_WIFI);
		return;
	}
	if (read_fd(wireless_fd, read_buffer, sizeof(read_buffer)) >= 0) {
		line = strstr(read_buffer, wifi_interface);
		if (line && (colon = strchr(line, ':')))
			(void)sscanf(colon + 1, "%*x %lf", &quality);
	}
	(void)snprintf(command, sizeof(command), "iw dev %s link 2>/dev/null",
	               wifi_interface);
	output = run_command(command);
	if (!output || strstr(output, "Not connected")) {
		(void)snprintf(wifi_text, sizeof(wifi_text),
		               "^c%s^%s not connected^d^", COLOR_GREY, ICON_WIFI);
		return;
	}
	percent = quality < 0.0 ? 0 : (long)(quality * 100.0 / 70.0 + 0.5);
	if (percent > 100)
		percent = 100;
	(void)snprintf(wifi_text, sizeof(wifi_text), "^c%s^%s %ld%%^d^",
	               COLOR_GREEN, ICON_WIFI, percent);
}

static void
refresh_cpu(void)
{
	static uint64_t previous_total;
	static uint64_t previous_idle;
	uint64_t values[10] = {0};
	uint64_t total = 0, idle, total_delta, idle_delta;
	int count, i;

	cpu_text[0] = '\0';
	if (read_fd(stat_fd, read_buffer, sizeof(read_buffer)) < 0)
		return;
	count = sscanf(read_buffer,
	               "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
	               &values[0], &values[1], &values[2], &values[3], &values[4],
	               &values[5], &values[6], &values[7], &values[8], &values[9]);
	if (count < 4)
		return;
	for (i = 0; i < count; i++)
		total += values[i];
	idle = values[3] + (count > 4 ? values[4] : 0);
	if (previous_total != 0 && total > previous_total) {
		total_delta = total - previous_total;
		idle_delta = idle - previous_idle;
		(void)snprintf(cpu_text, sizeof(cpu_text), "%s %llu%%", ICON_CPU,
		               (unsigned long long)((total_delta - idle_delta) * 100 /
		                                    total_delta));
	}
	previous_total = total;
	previous_idle = idle;
}

static void
refresh_ram(void)
{
	char *line;
	unsigned long total = 0, available = 0;
	double used_gib;

	ram_text[0] = '\0';
	if (read_fd(meminfo_fd, read_buffer, sizeof(read_buffer)) < 0)
		return;
	line = strstr(read_buffer, "MemTotal:");
	if (line)
		(void)sscanf(line, "MemTotal: %lu", &total);
	line = strstr(read_buffer, "MemAvailable:");
	if (line)
		(void)sscanf(line, "MemAvailable: %lu", &available);
	if (!total || available > total)
		return;
	used_gib = (double)(total - available) / 1048576.0;
	(void)snprintf(ram_text, sizeof(ram_text), "%s %.1fG",
	               ICON_RAM, used_gib);
}

static void
refresh_disk(void)
{
	struct statvfs fs;
	unsigned long long used_blocks;
	double used_gib;

	disk_text[0] = '\0';
	if (statvfs("/", &fs) != 0 || fs.f_blocks == 0)
		return;
	used_blocks = (unsigned long long)fs.f_blocks -
	              (unsigned long long)fs.f_bfree;
	used_gib = (double)used_blocks * (double)fs.f_frsize / 1073741824.0;
	(void)snprintf(disk_text, sizeof(disk_text), "%s %.1fG",
	               ICON_DISK, used_gib);
}

static void
refresh_battery(void)
{
	long capacity;
	char state[32];
	const char *color;
	int charging;

	battery_text[0] = '\0';
	if (read_long_fd(battery_capacity_fd, &capacity) < 0 ||
	    read_fd(battery_status_fd, state, sizeof(state)) < 0)
		return;
	charging = strncmp(state, "Charging", 8) == 0;
	if (capacity > 70)
		color = COLOR_GREEN;
	else if (capacity >= 30)
		color = COLOR_ORANGE;
	else
		color = COLOR_RED;
	(void)snprintf(battery_text, sizeof(battery_text),
	               "^c%s^%s %ld%%%s%s^d^", color, ICON_BATTERY, capacity,
	               charging ? " " ICON_CHARGING : "",
	               capacity < 15 ? " LOW" : "");
}

static void
refresh_time(void)
{
	time_t now;
	struct tm local;

	time_text[0] = '\0';
	now = time(NULL);
	if (localtime_r(&now, &local) &&
	    strftime(time_text, sizeof(time_text), ICON_TIME " %a %d %b %H:%M",
	             &local) == 0)
		time_text[0] = '\0';
}

static void
append_module(char *status, size_t size, const char *module)
{
	size_t used;

	if (!module[0])
		return;
	used = strlen(status);
	if (used >= size - 1)
		return;
	(void)snprintf(status + used, size - used, "%s%s",
	               used ? SEPARATOR : "", module);
}

static void
append_controlled_module(char *status, size_t size, const char *module,
                         char marker)
{
	size_t used;

	if (!module[0])
		return;
	used = strlen(status);
	if (used >= size - 1)
		return;
	(void)snprintf(status + used, size - used, "%s^%c^%s^%c^",
	               used ? SEPARATOR : "", marker, module, marker);
}

static void
close_files(void)
{
	int *fds[] = { &stat_fd, &meminfo_fd, &wireless_fd, &brightness_fd,
	               &max_brightness_fd, &battery_capacity_fd,
	               &battery_status_fd, &wifi_state_fd, &wifi_flags_fd };
	size_t i;

	for (i = 0; i < sizeof(fds) / sizeof(fds[0]); i++) {
		if (*fds[i] >= 0)
			close(*fds[i]);
		*fds[i] = -1;
	}
}

int
main(void)
{
	struct sigaction action;
	struct timespec tick = { 1, 0 };
	struct timespec now;
	sigset_t refresh_mask, previous_mask;
	time_t last_volume = 0, last_brightness = 0, last_wifi = 0;
	time_t last_cpu = 0, last_ram = 0, last_disk = 0;
	time_t last_battery = 0, last_time = 0;
	sig_atomic_t flags;
	char status[STATUS_SIZE] = "";
	char previous[STATUS_SIZE] = "";

	memset(&action, 0, sizeof(action));
	action.sa_handler = handle_signal;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGINT, &action, NULL) < 0 ||
	    sigaction(SIGTERM, &action, NULL) < 0 ||
	    sigaction(SIGHUP, &action, NULL) < 0 ||
	    sigaction(SIGRTMIN + 1, &action, NULL) < 0 ||
	    sigaction(SIGRTMIN + 2, &action, NULL) < 0 ||
	    sigaction(SIGRTMIN + 3, &action, NULL) < 0) {
		perror("sigaction");
		return 1;
	}
	sigemptyset(&refresh_mask);
	sigaddset(&refresh_mask, SIGRTMIN + 1);
	sigaddset(&refresh_mask, SIGRTMIN + 2);
	sigaddset(&refresh_mask, SIGRTMIN + 3);
	sigaddset(&refresh_mask, SIGHUP);

	detect_audio();
	discover_brightness();
	discover_battery();
	discover_wifi();
	load_theme_colors();
	refresh_bluetooth();
	stat_fd = open("/proc/stat", O_RDONLY | O_CLOEXEC);
	meminfo_fd = open("/proc/meminfo", O_RDONLY | O_CLOEXEC);
	wireless_fd = open("/proc/net/wireless", O_RDONLY | O_CLOEXEC);

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display\n");
		close_files();
		return 1;
	}

	while (running) {
		if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
			break;
		(void)sigprocmask(SIG_BLOCK, &refresh_mask, &previous_mask);
		flags = refresh_flags;
		refresh_flags = 0;
		(void)sigprocmask(SIG_SETMASK, &previous_mask, NULL);
		if (flags & REFRESH_THEME) {
			load_theme_colors();
			refresh_wifi();
			refresh_bluetooth();
			refresh_battery();
		}
		if (flags & REFRESH_NETWORK) {
			refresh_wifi();
			refresh_bluetooth();
			last_wifi = now.tv_sec;
		}
		if ((flags & REFRESH_VOLUME) || now.tv_sec - last_volume >= 30) {
			refresh_volume();
			last_volume = now.tv_sec;
		}
		if ((flags & REFRESH_BRIGHTNESS) ||
		    now.tv_sec - last_brightness >= 30) {
			refresh_brightness();
			last_brightness = now.tv_sec;
		}
		if (now.tv_sec - last_wifi >= 10) {
			refresh_wifi();
			refresh_bluetooth();
			last_wifi = now.tv_sec;
		}
		if (now.tv_sec - last_cpu >= 1) {
			refresh_cpu();
			last_cpu = now.tv_sec;
		}
		if (now.tv_sec - last_ram >= 2) {
			refresh_ram();
			last_ram = now.tv_sec;
		}
		if (now.tv_sec - last_disk >= 30) {
			refresh_disk();
			last_disk = now.tv_sec;
		}
		if (now.tv_sec - last_battery >= 15) {
			refresh_battery();
			last_battery = now.tv_sec;
		}
		if (now.tv_sec - last_time >= 1) {
			refresh_time();
			last_time = now.tv_sec;
		}

		status[0] = '\0';
		append_controlled_module(status, sizeof(status), volume_text, 'V');
		append_controlled_module(status, sizeof(status), brightness_text, 'B');
		append_controlled_module(status, sizeof(status), wifi_text, 'W');
		append_controlled_module(status, sizeof(status), bluetooth_text, 'L');
		append_module(status, sizeof(status), cpu_text);
		append_module(status, sizeof(status), ram_text);
		append_module(status, sizeof(status), disk_text);
		append_module(status, sizeof(status), battery_text);
		append_module(status, sizeof(status), time_text);
		if (strcmp(status, previous) != 0) {
			XStoreName(dpy, DefaultRootWindow(dpy), status);
			XSync(dpy, False);
			(void)snprintf(previous, sizeof(previous), "%s", status);
		}

		if (!running)
			break;
		(void)nanosleep(&tick, NULL);
	}

	XStoreName(dpy, DefaultRootWindow(dpy), "");
	XSync(dpy, False);
	XCloseDisplay(dpy);
	close_files();
	return 0;
}
