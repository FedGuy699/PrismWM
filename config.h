#pragma once

extern Display* display;
int parse_modifier(const std::string& mod); 

extern std::string xrandr_command;
extern std::vector<std::string> startup_commands;
extern std::map<std::pair<int, unsigned int>, std::string> keybindings;

void load_config(Display* dpy, Window root);
std::string get_config_path();
void show_config_created_bar(const std::string& message);
