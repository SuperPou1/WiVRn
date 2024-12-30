/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

class QProcess;

class adb : public QObject
{
	Q_OBJECT

public:
	class device
	{
		std::string _serial;
		std::string _state;
		std::map<std::string, std::string> _properties;

		friend class adb;

	public:
		device() = default;
		device(const device &) = default;
		device(device &&) = default;
		device & operator=(const device &) = default;
		device & operator=(device &&) = default;

		const auto & serial() const
		{
			return _serial;
		};

		const auto & state() const
		{
			return _state;
		};

		const auto & properties() const
		{
			return _properties;
		};

		bool operator==(const device & other) const
		{
			return _serial == other._serial;
		}

		operator bool() const
		{
			return _serial != "";
		}

		std::unique_ptr<QProcess> install(const std::filesystem::path & path);
		std::unique_ptr<QProcess> uninstall(const std::string & app);
		void start(const std::string & app, const std::string & action, const std::string & uri);
		void reverse_forward(int local_port, int device_port);
		std::vector<std::string> installed_apps();
	};

private:
	QTimer poll_devices_timer;
	std::unique_ptr<QProcess> poll_devices_process;
	std::vector<adb::device> _android_devices;

	void on_poll_devices_timeout();
	void on_poll_devices_process_finished(int exit_code, QProcess::ExitStatus exit_status);

public:
	adb();
	adb(const adb &) = delete;
	adb & operator=(const adb &) = delete;

	const std::vector<adb::device> & devices() const
	{
		return _android_devices;
	}

	void start()
	{
		poll_devices_timer.start();
	}

	void stop()
	{
		poll_devices_timer.stop();
	}

Q_SIGNALS:
	void android_devices_changed(const std::vector<adb::device> &);
};
