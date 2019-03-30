/*
 Copyright (c) 2019 Nia Alarie <nia@netbsd.org>
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/audioio.h>
#include <sys/ioctl.h>

#include <cdk.h>

#include <stdbool.h>

#define DEFAULT_MIXER_DEVICE	"/dev/mixer"

#define MAX_CONTROLS	(64)
#define MAX_CLASSES	(16)

#define MAX_CONTROL_LEN	(16)

#define PAIR_CLASS_BUTTONS_HL	(2)
#define PAIR_SLIDER		(3)
#define PAIR_ENUM_SET		(4)

struct aiomixer_control {
	char name[MAX_CONTROL_LEN];
	int dev;
	int type;
	int current_chan; /* for VALUE type */
	bool chans_unlocked; /* for VALUE type */
	union {
		struct audio_mixer_enum e;
		struct audio_mixer_set s;
		struct audio_mixer_value v;
	};
	union {
		CDKBUTTONBOX *enum_widget;
		CDKBUTTONBOX *set_widget;
		CDKSLIDER *value_widget[8];
	};
};

struct aiomixer_class {
	char name[MAX_AUDIO_DEV_LEN];
	int id;
	unsigned ncontrols;
	struct aiomixer_control controls[MAX_CONTROLS];
};

struct aiomixer {
	unsigned nclasses;
	struct aiomixer_class classes[MAX_CLASSES];
	unsigned class_index;
	unsigned control_index;
	CDKSCREEN *screen;
	CDKBUTTONBOX *class_buttons;
	int fd;
};

static void select_class(struct aiomixer *);
static void select_class_widget(struct aiomixer *, int);
static struct aiomixer_class *aiomixer_get_class(struct aiomixer *, int);
static void aiomixer_devinfo(struct aiomixer *);
static char **make_enum_list(struct audio_mixer_enum *);
static char **make_set_list(struct audio_mixer_set *);
static size_t sum_str_list_lengths(const char **, size_t);
static void create_class_widgets(struct aiomixer *, int);
static void destroy_class_widgets(struct aiomixer *);
static void enum_get_and_select(int, struct aiomixer_control *);
static void set_get_and_select(int, struct aiomixer_control *);
static void levels_get_and_set(int, struct aiomixer_control *);
static void set_enum(int, int, int);
static void set_set(int, int, int);
static void set_level(int, struct aiomixer_control *, int, int);
static int key_callback_slider(EObjectType, void *, void *, chtype);
static int key_callback_class_buttons(EObjectType, void *, void *, chtype);
static int key_callback_control_buttons(EObjectType, void *, void *, chtype);
static int key_callback_global(EObjectType, void *, void *, chtype);
static void add_directional_binds(struct aiomixer *, EObjectType, void *, BINDFN);
static void add_slider_binds(struct aiomixer *, void *);
static void add_class_button_binds(struct aiomixer *, void *);
static void add_control_button_binds(struct aiomixer *, void *);
static void add_global_binds(struct aiomixer *, EObjectType, void *);
static void usage(void);
static void quit(struct aiomixer *);

static struct aiomixer_class *
aiomixer_get_class(struct aiomixer *x, int class_id)
{
	for (unsigned i = 0; i < x->nclasses; ++i) {
		if (x->classes[i].id == class_id) {
			return &x->classes[i];
		}
	}
	return NULL;
}

static void
aiomixer_devinfo(struct aiomixer *x)
{
	struct mixer_devinfo m = {0};
	struct aiomixer_class *class = NULL;
	struct aiomixer_control *control = NULL;
	struct audio_mixer_enum e;
	struct audio_mixer_set s;
	struct audio_mixer_value v;
	int i;

	for (m.index = 0; ioctl(x->fd, AUDIO_MIXER_DEVINFO, &m) != -1; ++m.index) {
		switch (m.type) {
		case AUDIO_MIXER_CLASS:
			if (x->nclasses < MAX_CLASSES) {
				class = &x->classes[x->nclasses++];
				class->id = m.mixer_class;
				memcpy(class->name, m.label.name, MAX_AUDIO_DEV_LEN);
			}
			break;
		case AUDIO_MIXER_ENUM:
			e = m.un.e;
			class = aiomixer_get_class(x, m.mixer_class);
			if (class != NULL && class->ncontrols < MAX_CONTROLS) {
				control = &class->controls[class->ncontrols++];
				memcpy(control->name, m.label.name, MAX_AUDIO_DEV_LEN);
				control->type = AUDIO_MIXER_ENUM;
				control->dev = m.index;
				control->e.num_mem = e.num_mem;
				for (i = 0; i < e.num_mem; ++i) {
					control->e.member[i].label = e.member[i].label;
					control->e.member[i].ord = e.member[i].ord;
				}
			}
			break;
		case AUDIO_MIXER_SET:
			s = m.un.s;
			class = aiomixer_get_class(x, m.mixer_class);
			if (class != NULL && class->ncontrols < MAX_CONTROLS) {
				control = &class->controls[class->ncontrols++];
				memcpy(control->name, m.label.name, MAX_AUDIO_DEV_LEN);
				control->type = AUDIO_MIXER_SET;
				control->dev = m.index;
				control->s.num_mem = s.num_mem;
				for (i = 0; i < s.num_mem; ++i) {
					control->s.member[i].label = s.member[i].label;
					control->s.member[i].mask = s.member[i].mask;
				}
			}
			break;
		case AUDIO_MIXER_VALUE:
			v = m.un.v;
			class = aiomixer_get_class(x, m.mixer_class);
			if (class != NULL && class->ncontrols < MAX_CONTROLS) {
				control = &class->controls[class->ncontrols++];
				memcpy(control->name, m.label.name, MAX_AUDIO_DEV_LEN);
				control->type = AUDIO_MIXER_VALUE;
				control->dev = m.index;
				control->v.num_channels = v.num_channels;
				control->v.delta = v.delta;
			}
			break;
		}
	}
}

static char **
make_enum_list(struct audio_mixer_enum *e)
{
	char **list = calloc(e->num_mem, sizeof(char *));
	if (list == NULL) return NULL;
	for (int i = 0; i < e->num_mem; ++i) {
		list[i] = e->member[i].label.name;
	}
	return list;
}

static char **
make_set_list(struct audio_mixer_set *s)
{
	char **list = calloc(s->num_mem, sizeof(char *));
	if (list == NULL) return NULL;
	for (int i = 0; i < s->num_mem; ++i) {
		list[i] = s->member[i].label.name;
	}
	return list;
}

static size_t
sum_str_list_lengths(const char **list, size_t n)
{
	size_t i, total = 0;

	for (i = 0; i < n; ++i) {
		total += strlen(list[i]);
	}
	return total;
}

static void
enum_get_and_select(int fd, struct aiomixer_control *control)
{
	mixer_ctrl_t dev = {0};

	dev.dev = control->dev;

	(void)ioctl(fd, AUDIO_MIXER_READ, &dev);

	for (int i = 0; i < control->e.num_mem; ++i) {
		if (control->e.member[i].ord == dev.un.ord) {
			setCDKButtonboxCurrentButton(control->enum_widget, i);
			break;
		}
	}
}

static void
set_get_and_select(int fd, struct aiomixer_control *control)
{
	mixer_ctrl_t dev = {0};

	dev.dev = control->dev;

	(void)ioctl(fd, AUDIO_MIXER_READ, &dev);

	for (int i = 0; i < control->s.num_mem; ++i) {
		if (control->s.member[i].mask == dev.un.mask) {
			setCDKButtonboxCurrentButton(control->set_widget, i);
			break;
		}
	}
}

static void
levels_get_and_set(int fd, struct aiomixer_control *control)
{
	mixer_ctrl_t dev = {0};

	dev.dev = control->dev;
	(void)ioctl(fd, AUDIO_MIXER_READ, &dev);
	for (int chan = 0; chan < control->v.num_channels; ++chan) {
		setCDKSliderValue(control->value_widget[chan],
			dev.un.value.level[chan]);
	}
}

static void
set_enum(int fd, int dev_id, int ord)
{
	mixer_ctrl_t dev = {0};

	dev.dev = dev_id;
	dev.un.ord = ord;
	(void)ioctl(fd, AUDIO_MIXER_WRITE, &dev);
}

static void
set_set(int fd, int dev_id, int mask)
{
	mixer_ctrl_t dev = {0};

	dev.dev = dev_id;
	dev.un.mask = mask;
	(void)ioctl(fd, AUDIO_MIXER_WRITE, &dev);
}

static void 
add_global_binds(struct aiomixer *x, EObjectType type, void *object)
{
	unsigned int i;

	for (i = 1; i <= x->nclasses; ++i) {
		bindCDKObject(type, object, KEY_F0 + i, key_callback_global, x);
	}
	bindCDKObject(type, object, KEY_RESIZE, key_callback_global, x);
}

static void
add_directional_binds(struct aiomixer *x, EObjectType type, void *object, BINDFN fn)
{
	bindCDKObject(type, object, KEY_UP, fn, x);
	bindCDKObject(type, object, KEY_DOWN, fn, x);
	bindCDKObject(type, object, KEY_LEFT, fn, x);
	bindCDKObject(type, object, KEY_RIGHT, fn, x);
}

static void
add_slider_binds(struct aiomixer *x, void *object)
{
	add_global_binds(x, vSLIDER, object);
	add_directional_binds(x, vSLIDER, object, key_callback_slider);
	bindCDKObject(vSLIDER, object, 'u', key_callback_slider, x);
	bindCDKObject(vSLIDER, object, 'm', key_callback_slider, x);
}

static void
add_class_button_binds(struct aiomixer *x, void *object)
{
	add_global_binds(x, vBUTTONBOX, object);
	add_directional_binds(x, vBUTTONBOX, object, key_callback_class_buttons);
	bindCDKObject(vBUTTONBOX, object, 0x1b /* \e */, key_callback_class_buttons, x);
}

static void
add_control_button_binds(struct aiomixer *x, void *object)
{
	add_global_binds(x, vBUTTONBOX, object);
	add_directional_binds(x, vBUTTONBOX, object, key_callback_control_buttons);
}

static void
create_class_widgets(struct aiomixer *x, int y)
{
	char label[32];
	struct aiomixer_class *class = &x->classes[x->class_index];
	struct aiomixer_control *control;
	char **list;
	unsigned i;
	int width;
	char *title[] = { "</B/56>Controls<!56>" };

	drawCDKLabel(newCDKLabel(x->screen, 0, y, title, 1, false, false), false);
	y += 2;

	for (i = 0; i < class->ncontrols; ++i) {
		control = &class->controls[i];
		switch (control->type) {
		case AUDIO_MIXER_ENUM:
			if ((list = make_enum_list(&control->e)) != NULL) {
				snprintf(label, sizeof(label), "</16>%s<!16>", control->name);
				width = sum_str_list_lengths((const char **)list, control->e.num_mem)
					+ control->e.num_mem + 10;
				control->enum_widget = newCDKButtonbox(x->screen,
					0, y, 5, width,
					label, 1, control->e.num_mem,
					list, control->e.num_mem,
					COLOR_PAIR(PAIR_ENUM_SET) | A_BOLD, false, false);
				enum_get_and_select(x->fd, control);
				add_control_button_binds(x, control->enum_widget);
				drawCDKButtonbox(control->enum_widget, false);
			} else {
				control->enum_widget = NULL;
			}
			free(list);
			y += 3;
			break;
		case AUDIO_MIXER_SET:
			if ((list = make_set_list(&control->s)) != NULL) {
				snprintf(label, sizeof(label), "</16>%s<!16>", control->name);
				width = sum_str_list_lengths((const char **)list, control->s.num_mem)
					+ control->s.num_mem + 10;
				control->set_widget = newCDKButtonbox(x->screen,
					0, y, 5, width,
					label, 1, control->s.num_mem,
					list, control->s.num_mem,
					COLOR_PAIR(PAIR_ENUM_SET) | A_BOLD, false, false);
				set_get_and_select(x->fd, control);
				add_control_button_binds(x, control->set_widget);
				drawCDKButtonbox(control->set_widget, false);
			} else {
				control->set_widget = NULL;
			}
			free(list);
			y += 3;
			break;
		case AUDIO_MIXER_VALUE:
			for (int chan = 0; chan < control->v.num_channels; ++chan) {
				snprintf(label, sizeof(label), "</16>%s (channel %d)<!16>",
					control->name, chan);
				control->value_widget[chan] = newCDKSlider(x->screen, 0, y,
					label, "% ", '#' | COLOR_PAIR(PAIR_SLIDER) | A_BOLD,
					0, 50, 0, 255,
					control->v.delta, control->v.delta * 2,
					false, false);
				add_slider_binds(x, control->value_widget[chan]);
				y += 3;
			}
			levels_get_and_set(x->fd, control);
			for (int chan = 0; chan < control->v.num_channels; ++chan) {
				drawCDKSlider(control->value_widget[chan], false);
			}
			break;
		}
	}
}

static void
destroy_class_widgets(struct aiomixer *x)
{
	struct aiomixer_class *class = &x->classes[x->class_index];
	struct aiomixer_control *control;
	unsigned i;

	for (i = 0; i < class->ncontrols; ++i) {
		control = &class->controls[i];
		switch (control->type) {
		case AUDIO_MIXER_ENUM:
			destroyCDKButtonbox(control->enum_widget);
			control->enum_widget = NULL;
			break;
		case AUDIO_MIXER_SET:
			destroyCDKButtonbox(control->set_widget);
			control->set_widget = NULL;
			break;
		case AUDIO_MIXER_VALUE:
			for (int j = 0; j < control->v.num_channels; ++j) {
				destroyCDKButtonbox(control->value_widget[j]);
				control->value_widget[j] = NULL;
			}
			break;
		}
	}
}

static void
select_class_widget(struct aiomixer *x, int index)
{
	struct aiomixer_class *class = &x->classes[x->class_index];
	struct aiomixer_control *control;
	int result;

	if (index < 0 || class->ncontrols < 1) {
		select_class(x);
		return;
	}
	if ((unsigned)index >= class->ncontrols) {
		select_class_widget(x, 0);
		return;
	}
	control = &class->controls[index];
	x->control_index = index;
	switch (control->type) {
	case AUDIO_MIXER_ENUM:
		enum_get_and_select(x->fd, control);
		result = activateCDKButtonbox(control->enum_widget, false);
		if (result == -1) {
			select_class(x);
		} else {
			select_class_widget(x, index + 1);
		}
		break;
	case AUDIO_MIXER_SET:
		set_get_and_select(x->fd, control);
		result = activateCDKButtonbox(control->set_widget, false);
		if (result == -1) {
			select_class(x);
		} else {
			select_class_widget(x, index + 1);
		}
		break;
	case AUDIO_MIXER_VALUE:
		levels_get_and_set(x->fd, control);
		result = activateCDKSlider(control->value_widget[control->current_chan], false);
		if (result == -1) {
			select_class(x);
		} else {
			if (control->current_chan < (control->v.num_channels - 1)) {
				control->current_chan++;
				select_class_widget(x, index);
			} else {
				control->current_chan = 0;
				select_class_widget(x, index + 1);
			}
		}
		break;
	}
}

static void
select_class(struct aiomixer *x)
{
	int result;

	result = activateCDKButtonbox(x->class_buttons, false);
	destroy_class_widgets(x);
	if (result != -1) {
		if ((unsigned)result != x->class_index) {
			x->control_index = 0;
		}
		x->class_index = (unsigned)result;
	}
	create_class_widgets(x, 3);
	select_class_widget(x, 0);
}

static void
set_level(int fd, struct aiomixer_control *control, int level, int channel)
{
	mixer_ctrl_t dev = {0};
	int i;

	dev.dev = control->dev;

	if (!control->chans_unlocked) {
		for (i = 0; i < control->v.num_channels; ++i) {
			dev.un.value.level[i] = level;
			setCDKSliderValue(control->value_widget[i], level);
			drawCDKSlider(control->value_widget[i], false);
		}
	} else {
		(void)ioctl(fd, AUDIO_MIXER_READ, &dev);
		dev.un.value.level[channel] = level;
		setCDKSliderValue(control->value_widget[channel], level);
		drawCDKSlider(control->value_widget[channel], false);
	}

	(void)ioctl(fd, AUDIO_MIXER_WRITE, &dev);
}

static int key_callback_slider(EObjectType cdktype ,
	void *object, void *clientData, chtype key)
{
	struct aiomixer *x = clientData;
	struct aiomixer_class *class = &x->classes[x->class_index];
	struct aiomixer_control *control = &class->controls[x->control_index];
	CDKSLIDER *widget = object;
	int new_value;

	(void)cdktype; /* unused */
	switch (key) {
	case KEY_UP:
		if (control->current_chan > 0) {
			control->current_chan--;
			select_class_widget(x, x->control_index);
		} else {
			control->current_chan = 0;
			select_class_widget(x, x->control_index - 1);
		}
		break;
	case KEY_DOWN:
		if (control->current_chan < (control->v.num_channels - 1)) {
			control->current_chan++;
			select_class_widget(x, x->control_index);
		} else {
			control->current_chan = 0;
			select_class_widget(x, x->control_index + 1);
		}
		break;
	case KEY_LEFT:
		new_value = getCDKSliderValue(widget) - control->v.delta;
		if (new_value < getCDKSliderLowValue(widget)) {
			new_value = getCDKSliderLowValue(widget);
		}
		set_level(x->fd, control, new_value, control->current_chan);
		break;
	case KEY_RIGHT:
		new_value = getCDKSliderValue(widget) + control->v.delta;
		if (new_value > getCDKSliderHighValue(widget)) {
			new_value = getCDKSliderHighValue(widget);
		}
		set_level(x->fd, control, new_value, control->current_chan);
		break;
	case 'u':
		control->chans_unlocked = !control->chans_unlocked;
		break;
	case 'm':
		/* TODO: mute/unmute thing */
		break;
	}
	return false;
}

static int key_callback_class_buttons(EObjectType cdktype,
	void *object, void *clientData, chtype key)
{
	struct aiomixer *x = clientData;

	(void)cdktype; /* unused */
	(void)object; /* unused */
	switch (key) {
	case 0x1b: /* escape */
		quit(x);
		break;
	case KEY_UP:
		return true;
	case KEY_DOWN:
		select_class_widget(x, 0);
		break;
	case KEY_LEFT:
		destroy_class_widgets(x);
		x->class_index = (getCDKButtonboxCurrentButton(x->class_buttons) - 1) % x->nclasses;
		create_class_widgets(x, 3);
		break;
	case KEY_RIGHT:
		destroy_class_widgets(x);
		x->class_index = (getCDKButtonboxCurrentButton(x->class_buttons) + 1) % x->nclasses;
		create_class_widgets(x, 3);
		break;
	}
	return false;
}

static int key_callback_control_buttons(EObjectType cdktype,
	void *object, void *clientData, chtype key)
{
	struct aiomixer *x = clientData;
	struct aiomixer_class *class = &x->classes[x->class_index];
	struct aiomixer_control *control = &class->controls[x->control_index];
	CDKBUTTONBOX *widget = object;
	int current;

	(void)cdktype; /* unused */
	current = getCDKButtonboxCurrentButton(widget);
	switch (key) {
	case KEY_UP:
		select_class_widget(x, x->control_index - 1);
		break;
	case KEY_DOWN:
		select_class_widget(x, x->control_index + 1);
		break;
	case KEY_LEFT:
		current = (current - 1) % getCDKButtonboxButtonCount(widget);
		if (control->type == AUDIO_MIXER_SET) {
			set_set(x->fd, control->dev, control->s.member[current].mask);
		} else if (control->type == AUDIO_MIXER_ENUM) {
			set_enum(x->fd, control->dev, control->e.member[current].ord);
		}
		break;
	case KEY_RIGHT:
		current = (current + 1) % getCDKButtonboxButtonCount(widget);
		if (control->type == AUDIO_MIXER_SET) {
			set_set(x->fd, control->dev, control->s.member[current].mask);
		} else if (control->type == AUDIO_MIXER_ENUM) {
			set_enum(x->fd, control->dev, control->e.member[current].ord);
		}
		break;
	}
	return false;
}

static int key_callback_global(EObjectType cdktype,
	void *object, void *clientData, chtype key)
{
	struct aiomixer *x = clientData;

	(void)cdktype; /* unused */
	(void)object; /* unused */
	if (key > KEY_F0 && key <= (KEY_F0 + x->nclasses + 1)) {
		key = key - KEY_F0 - 1;
		destroy_class_widgets(x);
		setCDKButtonboxCurrentButton(x->class_buttons, key);
		drawCDKButtonboxButtons(x->class_buttons);
		x->class_index = key;
		create_class_widgets(x, 3);
		select_class_widget(x, 0);
		return false;
	}
	switch (key) {
	case KEY_RESIZE:
		destroy_class_widgets(x);
		drawCDKButtonboxButtons(x->class_buttons);
		create_class_widgets(x, 3);
		select_class_widget(x, 0);
		break;
	}
	return false;
}

static void
usage(void)
{
	fputs("aiomixer [-d device]\n", stderr);
	exit(1);
}

static void
quit(struct aiomixer *x)
{
	destroyCDKScreen(x->screen);
	endCDK();
	close(x->fd);
	exit(0);
}

int
main(int argc, char *argv[])
{
	struct aiomixer x = {0};
	char *title[] = { "AudioIO Mixer" };
	char *mixer_device = DEFAULT_MIXER_DEVICE;
	int ch;
	extern char *optarg;
	extern int optind;

	while ((ch = getopt(argc, argv, "d:")) != -1) {
		switch (ch) {
		case 'd':
			mixer_device = optarg;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if ((x.fd = open(mixer_device, O_RDWR)) == -1) {
		perror("open(mixer_device)");
		return 1;
	}

	aiomixer_devinfo(&x);

	char **class_names = calloc(sizeof(char *), x.nclasses);
	for (unsigned i = 0; i < x.nclasses; ++i) {
		class_names[i] = x.classes[i].name;
	}

	x.screen = initCDKScreen(NULL);
	initCDKColor();

	init_pair(PAIR_CLASS_BUTTONS_HL, COLOR_WHITE, COLOR_BLUE);
	init_pair(PAIR_SLIDER, COLOR_GREEN, COLOR_BLACK);
	init_pair(PAIR_ENUM_SET, COLOR_YELLOW, COLOR_BLACK);

	drawCDKLabel(newCDKLabel(x.screen, RIGHT, 0, title, 1, false, false), false);

	x.class_buttons = newCDKButtonbox(x.screen, 0, 0,
		2, sum_str_list_lengths((const char **)class_names, x.nclasses) + 10 + x.nclasses,
		"</B/56>Classes<!56>", 1, x.nclasses,
		class_names, x.nclasses,
		COLOR_PAIR(PAIR_CLASS_BUTTONS_HL), false, false);
	free(class_names);

	drawCDKButtonbox(x.class_buttons, false);

	add_class_button_binds(&x, x.class_buttons);

	create_class_widgets(&x, 3);
	select_class_widget(&x, 0);

	quit(&x);
	return 0; /* never reached */
}

