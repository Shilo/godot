/*************************************************************************/
/*  os_linuxbsd.cpp                                                      */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2022 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2022 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "os_linuxbsd.h"

#include "core/io/dir_access.h"
#include "main/main.h"
#include "servers/display_server.h"

#ifdef X11_ENABLED
#include "display_server_x11.h"
#endif

#ifdef HAVE_MNTENT
#include <mntent.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef FONTCONFIG_ENABLED
#include <fontconfig/fontconfig.h>
#endif

void OS_LinuxBSD::alert(const String &p_alert, const String &p_title) {
	const char *message_programs[] = { "zenity", "kdialog", "Xdialog", "xmessage" };

	String path = get_environment("PATH");
	Vector<String> path_elems = path.split(":", false);
	String program;

	for (int i = 0; i < path_elems.size(); i++) {
		for (uint64_t k = 0; k < sizeof(message_programs) / sizeof(char *); k++) {
			String tested_path = path_elems[i].plus_file(message_programs[k]);

			if (FileAccess::exists(tested_path)) {
				program = tested_path;
				break;
			}
		}

		if (program.length()) {
			break;
		}
	}

	List<String> args;

	if (program.ends_with("zenity")) {
		args.push_back("--error");
		args.push_back("--width");
		args.push_back("500");
		args.push_back("--title");
		args.push_back(p_title);
		args.push_back("--text");
		args.push_back(p_alert);
	}

	if (program.ends_with("kdialog")) {
		args.push_back("--error");
		args.push_back(p_alert);
		args.push_back("--title");
		args.push_back(p_title);
	}

	if (program.ends_with("Xdialog")) {
		args.push_back("--title");
		args.push_back(p_title);
		args.push_back("--msgbox");
		args.push_back(p_alert);
		args.push_back("0");
		args.push_back("0");
	}

	if (program.ends_with("xmessage")) {
		args.push_back("-center");
		args.push_back("-title");
		args.push_back(p_title);
		args.push_back(p_alert);
	}

	if (program.length()) {
		execute(program, args);
	} else {
		print_line(p_alert);
	}
}

void OS_LinuxBSD::initialize() {
	crash_handler.initialize();

	OS_Unix::initialize_core();
}

void OS_LinuxBSD::initialize_joypads() {
#ifdef JOYDEV_ENABLED
	joypad = memnew(JoypadLinux(Input::get_singleton()));
#endif
}

String OS_LinuxBSD::get_unique_id() const {
	static String machine_id;
	if (machine_id.is_empty()) {
		Ref<FileAccess> f = FileAccess::open("/etc/machine-id", FileAccess::READ);
		if (f.is_valid()) {
			while (machine_id.is_empty() && !f->eof_reached()) {
				machine_id = f->get_line().strip_edges();
			}
		}
	}
	return machine_id;
}

String OS_LinuxBSD::get_processor_name() const {
	Ref<FileAccess> f = FileAccess::open("/proc/cpuinfo", FileAccess::READ);
	ERR_FAIL_COND_V_MSG(f.is_null(), "", String("Couldn't open `/proc/cpuinfo` to get the CPU model name. Returning an empty string."));

	while (!f->eof_reached()) {
		const String line = f->get_line();
		if (line.find("model name") != -1) {
			return line.split(":")[1].strip_edges();
		}
	}

	ERR_FAIL_V_MSG("", String("Couldn't get the CPU model name from `/proc/cpuinfo`. Returning an empty string."));
}

void OS_LinuxBSD::finalize() {
	if (main_loop) {
		memdelete(main_loop);
	}
	main_loop = nullptr;

#ifdef ALSAMIDI_ENABLED
	driver_alsamidi.close();
#endif

#ifdef JOYDEV_ENABLED
	if (joypad) {
		memdelete(joypad);
	}
#endif
}

MainLoop *OS_LinuxBSD::get_main_loop() const {
	return main_loop;
}

void OS_LinuxBSD::delete_main_loop() {
	if (main_loop) {
		memdelete(main_loop);
	}
	main_loop = nullptr;
}

void OS_LinuxBSD::set_main_loop(MainLoop *p_main_loop) {
	main_loop = p_main_loop;
}

String OS_LinuxBSD::get_name() const {
#ifdef __linux__
	return "Linux";
#elif defined(__FreeBSD__)
	return "FreeBSD";
#elif defined(__NetBSD__)
	return "NetBSD";
#elif defined(__OpenBSD__)
	return "OpenBSD";
#else
	return "BSD";
#endif
}

Error OS_LinuxBSD::shell_open(String p_uri) {
	Error ok;
	int err_code;
	List<String> args;
	args.push_back(p_uri);

	// Agnostic
	ok = execute("xdg-open", args, nullptr, &err_code);
	if (ok == OK && !err_code) {
		return OK;
	} else if (err_code == 2) {
		return ERR_FILE_NOT_FOUND;
	}
	// GNOME
	args.push_front("open"); // The command is `gio open`, so we need to add it to args
	ok = execute("gio", args, nullptr, &err_code);
	if (ok == OK && !err_code) {
		return OK;
	} else if (err_code == 2) {
		return ERR_FILE_NOT_FOUND;
	}
	args.pop_front();
	ok = execute("gvfs-open", args, nullptr, &err_code);
	if (ok == OK && !err_code) {
		return OK;
	} else if (err_code == 2) {
		return ERR_FILE_NOT_FOUND;
	}
	// KDE
	ok = execute("kde-open5", args, nullptr, &err_code);
	if (ok == OK && !err_code) {
		return OK;
	}
	ok = execute("kde-open", args, nullptr, &err_code);
	return !err_code ? ok : FAILED;
}

bool OS_LinuxBSD::_check_internal_feature_support(const String &p_feature) {
	return p_feature == "pc";
}

uint64_t OS_LinuxBSD::get_embedded_pck_offset() const {
	Ref<FileAccess> f = FileAccess::open(get_executable_path(), FileAccess::READ);
	if (f.is_null()) {
		return 0;
	}

	// Read and check ELF magic number.
	{
		uint32_t magic = f->get_32();
		if (magic != 0x464c457f) { // 0x7F + "ELF"
			return 0;
		}
	}

	// Read program architecture bits from class field.
	int bits = f->get_8() * 32;

	// Get info about the section header table.
	int64_t section_table_pos;
	int64_t section_header_size;
	if (bits == 32) {
		section_header_size = 40;
		f->seek(0x20);
		section_table_pos = f->get_32();
		f->seek(0x30);
	} else { // 64
		section_header_size = 64;
		f->seek(0x28);
		section_table_pos = f->get_64();
		f->seek(0x3c);
	}
	int num_sections = f->get_16();
	int string_section_idx = f->get_16();

	// Load the strings table.
	uint8_t *strings;
	{
		// Jump to the strings section header.
		f->seek(section_table_pos + string_section_idx * section_header_size);

		// Read strings data size and offset.
		int64_t string_data_pos;
		int64_t string_data_size;
		if (bits == 32) {
			f->seek(f->get_position() + 0x10);
			string_data_pos = f->get_32();
			string_data_size = f->get_32();
		} else { // 64
			f->seek(f->get_position() + 0x18);
			string_data_pos = f->get_64();
			string_data_size = f->get_64();
		}

		// Read strings data.
		f->seek(string_data_pos);
		strings = (uint8_t *)memalloc(string_data_size);
		if (!strings) {
			return 0;
		}
		f->get_buffer(strings, string_data_size);
	}

	// Search for the "pck" section.
	int64_t off = 0;
	for (int i = 0; i < num_sections; ++i) {
		int64_t section_header_pos = section_table_pos + i * section_header_size;
		f->seek(section_header_pos);

		uint32_t name_offset = f->get_32();
		if (strcmp((char *)strings + name_offset, "pck") == 0) {
			if (bits == 32) {
				f->seek(section_header_pos + 0x10);
				off = f->get_32();
			} else { // 64
				f->seek(section_header_pos + 0x18);
				off = f->get_64();
			}
			break;
		}
	}
	memfree(strings);

	return off;
}

Vector<String> OS_LinuxBSD::get_system_fonts() const {
#ifdef FONTCONFIG_ENABLED
	HashSet<String> font_names;
	Vector<String> ret;

	FcConfig *config = FcInitLoadConfigAndFonts();
	ERR_FAIL_COND_V(!config, ret);

	FcObjectSet *object_set = FcObjectSetBuild(FC_FAMILY, nullptr);
	ERR_FAIL_COND_V(!object_set, ret);

	static const char *allowed_formats[] = { "TrueType", "CFF" };
	for (size_t i = 0; i < sizeof(allowed_formats) / sizeof(const char *); i++) {
		FcPattern *pattern = FcPatternCreate();
		ERR_CONTINUE(!pattern);

		FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
		FcPatternAddString(pattern, FC_FONTFORMAT, reinterpret_cast<const FcChar8 *>(allowed_formats[i]));

		FcFontSet *font_set = FcFontList(config, pattern, object_set);
		if (font_set) {
			for (int j = 0; j < font_set->nfont; j++) {
				char *family_name = nullptr;
				if (FcPatternGetString(font_set->fonts[j], FC_FAMILY, 0, reinterpret_cast<FcChar8 **>(&family_name)) == FcResultMatch) {
					if (family_name) {
						font_names.insert(String::utf8(family_name));
					}
				}
			}
			FcFontSetDestroy(font_set);
		}
		FcPatternDestroy(pattern);
	}
	FcObjectSetDestroy(object_set);

	for (const String &E : font_names) {
		ret.push_back(E);
	}
	return ret;
#else
	ERR_FAIL_V_MSG(Vector<String>(), "Godot was compiled without fontconfig, system font support is disabled.");
#endif
}

String OS_LinuxBSD::get_system_font_path(const String &p_font_name, bool p_bold, bool p_italic) const {
#ifdef FONTCONFIG_ENABLED
	String ret;

	FcConfig *config = FcInitLoadConfigAndFonts();
	ERR_FAIL_COND_V(!config, ret);

	FcObjectSet *object_set = FcObjectSetBuild(FC_FAMILY, FC_FILE, nullptr);
	ERR_FAIL_COND_V(!object_set, ret);

	FcPattern *pattern = FcPatternCreate();
	if (pattern) {
		FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
		FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>(p_font_name.utf8().get_data()));
		FcPatternAddInteger(pattern, FC_WEIGHT, p_bold ? FC_WEIGHT_BOLD : FC_WEIGHT_NORMAL);
		FcPatternAddInteger(pattern, FC_SLANT, p_italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);

		FcConfigSubstitute(0, pattern, FcMatchPattern);
		FcDefaultSubstitute(pattern);

		FcResult result;
		FcPattern *match = FcFontMatch(0, pattern, &result);
		if (match) {
			char *file_name = nullptr;
			if (FcPatternGetString(match, FC_FILE, 0, reinterpret_cast<FcChar8 **>(&file_name)) == FcResultMatch) {
				if (file_name) {
					ret = String::utf8(file_name);
				}
			}

			FcPatternDestroy(match);
		}
		FcPatternDestroy(pattern);
	}
	FcObjectSetDestroy(object_set);
	return ret;
#else
	ERR_FAIL_V_MSG(String(), "Godot was compiled without fontconfig, system font support is disabled.");
#endif
}

String OS_LinuxBSD::get_config_path() const {
	if (has_environment("XDG_CONFIG_HOME")) {
		if (get_environment("XDG_CONFIG_HOME").is_absolute_path()) {
			return get_environment("XDG_CONFIG_HOME");
		} else {
			WARN_PRINT_ONCE("`XDG_CONFIG_HOME` is a relative path. Ignoring its value and falling back to `$HOME/.config` or `.` per the XDG Base Directory specification.");
			return has_environment("HOME") ? get_environment("HOME").plus_file(".config") : ".";
		}
	} else if (has_environment("HOME")) {
		return get_environment("HOME").plus_file(".config");
	} else {
		return ".";
	}
}

String OS_LinuxBSD::get_data_path() const {
	if (has_environment("XDG_DATA_HOME")) {
		if (get_environment("XDG_DATA_HOME").is_absolute_path()) {
			return get_environment("XDG_DATA_HOME");
		} else {
			WARN_PRINT_ONCE("`XDG_DATA_HOME` is a relative path. Ignoring its value and falling back to `$HOME/.local/share` or `get_config_path()` per the XDG Base Directory specification.");
			return has_environment("HOME") ? get_environment("HOME").plus_file(".local/share") : get_config_path();
		}
	} else if (has_environment("HOME")) {
		return get_environment("HOME").plus_file(".local/share");
	} else {
		return get_config_path();
	}
}

String OS_LinuxBSD::get_cache_path() const {
	if (has_environment("XDG_CACHE_HOME")) {
		if (get_environment("XDG_CACHE_HOME").is_absolute_path()) {
			return get_environment("XDG_CACHE_HOME");
		} else {
			WARN_PRINT_ONCE("`XDG_CACHE_HOME` is a relative path. Ignoring its value and falling back to `$HOME/.cache` or `get_config_path()` per the XDG Base Directory specification.");
			return has_environment("HOME") ? get_environment("HOME").plus_file(".cache") : get_config_path();
		}
	} else if (has_environment("HOME")) {
		return get_environment("HOME").plus_file(".cache");
	} else {
		return get_config_path();
	}
}

String OS_LinuxBSD::get_system_dir(SystemDir p_dir, bool p_shared_storage) const {
	String xdgparam;

	switch (p_dir) {
		case SYSTEM_DIR_DESKTOP: {
			xdgparam = "DESKTOP";
		} break;
		case SYSTEM_DIR_DCIM: {
			xdgparam = "PICTURES";

		} break;
		case SYSTEM_DIR_DOCUMENTS: {
			xdgparam = "DOCUMENTS";

		} break;
		case SYSTEM_DIR_DOWNLOADS: {
			xdgparam = "DOWNLOAD";

		} break;
		case SYSTEM_DIR_MOVIES: {
			xdgparam = "VIDEOS";

		} break;
		case SYSTEM_DIR_MUSIC: {
			xdgparam = "MUSIC";

		} break;
		case SYSTEM_DIR_PICTURES: {
			xdgparam = "PICTURES";

		} break;
		case SYSTEM_DIR_RINGTONES: {
			xdgparam = "MUSIC";

		} break;
	}

	String pipe;
	List<String> arg;
	arg.push_back(xdgparam);
	Error err = const_cast<OS_LinuxBSD *>(this)->execute("xdg-user-dir", arg, &pipe);
	if (err != OK) {
		return ".";
	}
	return pipe.strip_edges();
}

void OS_LinuxBSD::run() {
	force_quit = false;

	if (!main_loop) {
		return;
	}

	main_loop->initialize();

	//uint64_t last_ticks=get_ticks_usec();

	//int frames=0;
	//uint64_t frame=0;

	while (!force_quit) {
		DisplayServer::get_singleton()->process_events(); // get rid of pending events
#ifdef JOYDEV_ENABLED
		joypad->process_joypads();
#endif
		if (Main::iteration()) {
			break;
		}
	}

	main_loop->finalize();
}

void OS_LinuxBSD::disable_crash_handler() {
	crash_handler.disable();
}

bool OS_LinuxBSD::is_disable_crash_handler() const {
	return crash_handler.is_disabled();
}

static String get_mountpoint(const String &p_path) {
	struct stat s;
	if (stat(p_path.utf8().get_data(), &s)) {
		return "";
	}

#ifdef HAVE_MNTENT
	dev_t dev = s.st_dev;
	FILE *fd = setmntent("/proc/mounts", "r");
	if (!fd) {
		return "";
	}

	struct mntent mnt;
	char buf[1024];
	size_t buflen = 1024;
	while (getmntent_r(fd, &mnt, buf, buflen)) {
		if (!stat(mnt.mnt_dir, &s) && s.st_dev == dev) {
			endmntent(fd);
			return String(mnt.mnt_dir);
		}
	}

	endmntent(fd);
#endif
	return "";
}

Error OS_LinuxBSD::move_to_trash(const String &p_path) {
	String path = p_path.rstrip("/"); // Strip trailing slash when path points to a directory

	int err_code;
	List<String> args;
	args.push_back(path);
	args.push_front("trash"); // The command is `gio trash <file_name>` so we need to add it to args.
	Error result = execute("gio", args, nullptr, &err_code); // For GNOME based machines.
	if (result == OK && !err_code) {
		return OK;
	} else if (err_code == 2) {
		return ERR_FILE_NOT_FOUND;
	}

	args.pop_front();
	args.push_front("move");
	args.push_back("trash:/"); // The command is `kioclient5 move <file_name> trash:/`.
	result = execute("kioclient5", args, nullptr, &err_code); // For KDE based machines.
	if (result == OK && !err_code) {
		return OK;
	} else if (err_code == 2) {
		return ERR_FILE_NOT_FOUND;
	}

	args.pop_front();
	args.pop_back();
	result = execute("gvfs-trash", args, nullptr, &err_code); // For older Linux machines.
	if (result == OK && !err_code) {
		return OK;
	} else if (err_code == 2) {
		return ERR_FILE_NOT_FOUND;
	}

	// If the commands `kioclient5`, `gio` or `gvfs-trash` don't exist on the system we do it manually.
	String trash_path = "";
	String mnt = get_mountpoint(path);

	// If there is a directory "[Mountpoint]/.Trash-[UID], use it as the trash can.
	if (!mnt.is_empty()) {
		String mountpoint_trash_path(mnt + "/.Trash-" + itos(getuid()));
		struct stat s;
		if (!stat(mountpoint_trash_path.utf8().get_data(), &s)) {
			trash_path = mountpoint_trash_path;
		}
	}

	// Otherwise, if ${XDG_DATA_HOME} is defined, use "${XDG_DATA_HOME}/Trash" as the trash can.
	if (trash_path.is_empty()) {
		char *dhome = getenv("XDG_DATA_HOME");
		if (dhome) {
			trash_path = String::utf8(dhome) + "/Trash";
		}
	}

	// Otherwise, if ${HOME} is defined, use "${HOME}/.local/share/Trash" as the trash can.
	if (trash_path.is_empty()) {
		char *home = getenv("HOME");
		if (home) {
			trash_path = String::utf8(home) + "/.local/share/Trash";
		}
	}

	// Issue an error if none of the previous locations is appropriate for the trash can.
	ERR_FAIL_COND_V_MSG(trash_path.is_empty(), FAILED, "Could not determine the trash can location");

	// Create needed directories for decided trash can location.
	{
		Ref<DirAccess> dir_access = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		Error err = dir_access->make_dir_recursive(trash_path);

		// Issue an error if trash can is not created properly.
		ERR_FAIL_COND_V_MSG(err != OK, err, "Could not create the trash path \"" + trash_path + "\"");
		err = dir_access->make_dir_recursive(trash_path + "/files");
		ERR_FAIL_COND_V_MSG(err != OK, err, "Could not create the trash path \"" + trash_path + "/files\"");
		err = dir_access->make_dir_recursive(trash_path + "/info");
		ERR_FAIL_COND_V_MSG(err != OK, err, "Could not create the trash path \"" + trash_path + "/info\"");
	}

	// The trash can is successfully created, now we check that we don't exceed our file name length limit.
	// If the file name is too long trim it so we can add the identifying number and ".trashinfo".
	// Assumes that the file name length limit is 255 characters.
	String file_name = path.get_file();
	if (file_name.length() > 240) {
		file_name = file_name.substr(0, file_name.length() - 15);
	}

	String dest_path = trash_path + "/files/" + file_name;
	struct stat buff;
	int id_number = 0;
	String fn = file_name;

	// Checks if a resource with the same name already exist in the trash can,
	// if there is, add an identifying number to our resource's name.
	while (stat(dest_path.utf8().get_data(), &buff) == 0) {
		id_number++;

		// Added a limit to check for identically named files already on the trash can
		// if there are too many it could make the editor unresponsive.
		ERR_FAIL_COND_V_MSG(id_number > 99, FAILED, "Too many identically named resources already in the trash can.");
		fn = file_name + "." + itos(id_number);
		dest_path = trash_path + "/files/" + fn;
	}
	file_name = fn;

	String renamed_path = path.get_base_dir() + "/" + file_name;

	// Generates the .trashinfo file
	OS::Date date = OS::get_singleton()->get_date(false);
	OS::Time time = OS::get_singleton()->get_time(false);
	String timestamp = vformat("%04d-%02d-%02dT%02d:%02d:", date.year, (int)date.month, date.day, time.hour, time.minute);
	timestamp = vformat("%s%02d", timestamp, time.second); // vformat only supports up to 6 arguments.
	String trash_info = "[Trash Info]\nPath=" + path.uri_encode() + "\nDeletionDate=" + timestamp + "\n";
	{
		Error err;
		{
			Ref<FileAccess> file = FileAccess::open(trash_path + "/info/" + file_name + ".trashinfo", FileAccess::WRITE, &err);
			ERR_FAIL_COND_V_MSG(err != OK, err, "Can't create trashinfo file: \"" + trash_path + "/info/" + file_name + ".trashinfo\"");
			file->store_string(trash_info);
		}

		// Rename our resource before moving it to the trash can.
		Ref<DirAccess> dir_access = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		err = dir_access->rename(path, renamed_path);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Can't rename file \"" + path + "\" to \"" + renamed_path + "\"");
	}

	// Move the given resource to the trash can.
	// Do not use DirAccess:rename() because it can't move files across multiple mountpoints.
	List<String> mv_args;
	mv_args.push_back(renamed_path);
	mv_args.push_back(trash_path + "/files");
	{
		int retval;
		Error err = execute("mv", mv_args, nullptr, &retval);

		// Issue an error if "mv" failed to move the given resource to the trash can.
		if (err != OK || retval != 0) {
			ERR_PRINT("move_to_trash: Could not move the resource \"" + path + "\" to the trash can \"" + trash_path + "/files\"");
			Ref<DirAccess> dir_access = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
			err = dir_access->rename(renamed_path, path);
			ERR_FAIL_COND_V_MSG(err != OK, err, "Could not rename \"" + renamed_path + "\" back to its original name: \"" + path + "\"");
			return FAILED;
		}
	}
	return OK;
}

OS_LinuxBSD::OS_LinuxBSD() {
	main_loop = nullptr;
	force_quit = false;

#ifdef PULSEAUDIO_ENABLED
	AudioDriverManager::add_driver(&driver_pulseaudio);
#endif

#ifdef ALSA_ENABLED
	AudioDriverManager::add_driver(&driver_alsa);
#endif

#ifdef X11_ENABLED
	DisplayServerX11::register_x11_driver();
#endif
}
