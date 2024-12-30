/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <CLI/CLI.hpp>
#include <format>
#include <ranges>
#include <systemd/sd-bus.h>
#include <type_traits>

struct deleter
{
	void operator()(sd_bus * p)
	{
		sd_bus_unref(p);
	}
	void operator()(sd_bus_message * p)
	{
		sd_bus_message_unref(p);
	}
};

using sd_bus_ptr = std::unique_ptr<sd_bus, deleter>;
using sd_bus_message_ptr = std::unique_ptr<sd_bus_message, deleter>;

const char * destination = "io.github.wivrn.Server";
const char * path = "/io/github/wivrn/Server";
const char * interface = "io.github.wivrn.Server";

sd_bus_ptr get_user_bus()
{
	sd_bus * bus = nullptr;
	int ret = sd_bus_open_user(&bus);
	if (ret < 0)
		throw std::runtime_error(std::string("failed to connect to dbus: ") + strerror(-ret));

	return sd_bus_ptr(bus);
}

template <typename... Args>
sd_bus_message_ptr call_method(const sd_bus_ptr & bus, const char * member, const char * signature, Args &&... args)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message * msg = nullptr;
	int ret = sd_bus_call_method(bus.get(), destination, path, interface, member, &error, &msg, signature, std::forward<Args>(args)...);
	if (ret < 0)
	{
		std::runtime_error e(std::string("call to ") + member + " failed: " + error.message);
		sd_bus_error_free(&error);
		throw e;
	}
	return sd_bus_message_ptr(msg);
}

sd_bus_message_ptr get_property(const sd_bus_ptr & bus, const char * member, const char * signature)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message * msg = nullptr;
	int ret = sd_bus_get_property(bus.get(), destination, path, interface, member, &error, &msg, signature);
	if (ret < 0)
	{
		std::runtime_error e(std::string("read property ") + member + " failed: " + error.message);
		sd_bus_error_free(&error);
		throw e;
	}
	return sd_bus_message_ptr(msg);
}

template <typename Rng, typename value_type = std::remove_cvref_t<decltype(*std::declval<Rng>().begin())>>
void print_table(const std::array<std::string, std::tuple_size_v<value_type>> & header, const Rng & data)
{
	constexpr int Columns = std::tuple_size_v<value_type>;

	std::array<size_t, Columns> column_size{};

	for (auto [size, label]: std::views::zip(column_size, header))
	{
		size = std::max(size, label.size());
	}

	std::vector<std::array<std::string, Columns>> lines;
	for (const auto & line: data)
	{
		std::array<std::string, Columns> & line_str = lines.emplace_back();

		std::apply([&](auto &&... args) {int col = 0; ((line_str[col++] = std::format("{}", args)), ...); }, line);

		for (auto [size, label]: std::views::zip(column_size, line_str))
		{
			size = std::max(size, label.size());
		}
	}

	std::cout << "\e[1m";
	for (auto [size, label]: std::views::zip(column_size, header))
	{
		std::cout << std::left << std::setw(size) << label << " ";
	}
	std::cout << "\e[0m\n";

	for (const auto & line: lines)
	{
		for (auto [size, label]: std::views::zip(column_size, line))
		{
			std::cout << std::left << std::setw(size) << label << " ";
		}
		std::cout << "\n";
	}
}

std::vector<std::pair<std::string, std::string>> get_keys(const sd_bus_ptr & bus)
{
	auto msg = get_property(bus,
	                        "KnownKeys",
	                        "a(ss)");

	int ret = sd_bus_message_enter_container(msg.get(), 'a', "(ss)");
	if (ret < 0)
		throw std::system_error(-ret, std::system_category(), "Failed to get paired headsets");

	std::vector<std::pair<std::string, std::string>> values;
	while (true)
	{
		char * name;
		char * key;
		int ret = sd_bus_message_read(msg.get(), "(ss)", &name, &key);
		if (ret == 0)
			break;
		if (ret < 0)
			throw std::system_error(-ret, std::system_category(), "Failed to get paired headset details");

		values.emplace_back(name, key);
	}

	return values;
}

void pair(int duration)
{
	if (duration == 0)
	{
		call_method(get_user_bus(),
		            "DisablePairing",
		            "");
	}
	else
	{
		auto pin_msg = call_method(get_user_bus(),
		                           "EnablePairing",
		                           "i",
		                           duration * 60);

		const char * pin;
		int ret = sd_bus_message_read(pin_msg.get(), "s", &pin);
		if (ret < 0)
			throw std::system_error(-ret, std::system_category(), "Failed to read PIN");

		std::cout << "PIN: " << pin << std::endl;
	}
}

void unpair(int headset_id)
{
	auto bus = get_user_bus();
	auto values = get_keys(bus);

	if (headset_id < 1 or headset_id > values.size())
		throw std::runtime_error(std::format("Invalid headset number: {}", headset_id));

	call_method(bus, "RevokeKey", "s", values.at(headset_id - 1).second.c_str());
}

void list_paired(bool show_keys)
{
	auto values = get_keys(get_user_bus());

	if (values.empty())
	{
		std::cout << "No paired headset" << std::endl;
	}
	else
	{
		if (show_keys)
			print_table({"", "Headset name", "Public key"}, std::views::zip(std::views::iota(1), values | std::views::elements<0>, values | std::views::elements<1>));
		else
			print_table({"", "Headset name"}, std::views::zip(std::views::iota(1), values | std::views::elements<0>));
	}
}

void rename(int headset_id, const std::string & headset_name)
{
	auto bus = get_user_bus();
	auto values = get_keys(bus);

	if (headset_id < 1 or headset_id > values.size())
		throw std::runtime_error(std::format("Invalid headset number: {}", headset_id));

	call_method(bus, "RenameKey", "ss", values.at(headset_id - 1).second.c_str(), headset_name.c_str());
}

void stop_server()
{
	call_method(get_user_bus(), "Quit", "");
}

void disconnect()
{
	call_method(get_user_bus(), "Disconnect", "");
}

int main(int argc, char ** argv)
{
	CLI::App app;

	app.require_subcommand(1);
	app.failure_message(CLI::FailureMessage::help);

	int pairing_duration;
	auto pair_command = app.add_subcommand("pair", "Allow a new headset to connect")
	                            ->callback([&]() { return pair(pairing_duration); });
	auto duration = pair_command
	                        ->add_option("--duration,-d", pairing_duration, "Duration in minutes to allow new connections")
	                        ->transform(CLI::Validator([](std::string & input) {
		                        if (input.starts_with("-"))
			                        throw CLI::ValidationError("duration must be positive");
		                        if (input == "unlimited")
			                        input = "-1";
		                        return "";
	                        },
	                                                   "",
	                                                   ""))
	                        ->default_val(2)
	                        ->option_text("INT|unlimited");

	int headset_id;
	auto unpair_command = app.add_subcommand("unpair", "Remove a headset")
	                              ->callback([&]() { return unpair(headset_id); });
	unpair_command->add_option("HEADSET", headset_id, "Headset ID from the list-paired subcommand")->required();

	std::string headset_name;
	auto rename_command = app.add_subcommand("rename", "Rename a headset")
	                              ->callback([&]() { return rename(headset_id, headset_name); });
	rename_command->add_option("HEADSET", headset_id, "Headset ID from the list-paired subcommand")->required();
	rename_command->add_option("NAME", headset_name, "New headset name")->required();

	bool show_keys = false;
	auto list_command = app.add_subcommand("list-paired", "List headsets allowed to connect")
	                            ->callback([&]() { list_paired(show_keys); });

	list_command->add_flag("--keys, -k", show_keys, "Show public keys");

	app.add_subcommand("stop-server", "Stop wivrn-server process")
	        ->callback(stop_server);

	app.add_subcommand("disconnect", "Disconnect headset")
	        ->callback(disconnect);

	try
	{
		CLI11_PARSE(app, argc, argv);
	}
	catch (std::exception & e)
	{
		std::cerr << e.what() << std::endl;
		return 1;
	}
}
