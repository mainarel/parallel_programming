#define WIN32_LEAN_AND_MEAN
#undef UNICODE

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <Windows.h>
#include <WinSock2.h>
#include <ws2tcpip.h>

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

static const std::filesystem::path INPUT_FILEPATH{ "img03.bmp" };
static constexpr auto HEADER_SIZE{ 54U };
static constexpr auto CLIENTS_NUM{ 5U };

int client();
int server(std::string arg);
uint64_t proceed(const std::vector<char>& data);

int main(int argc, char* argv[])
{
	if (argc >= 2)
		return client();

	return server(argv[0]);
}

int client()
{
	std::cout << "Client created\n";

	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	struct addrinfo hints, * result = NULL;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	getaddrinfo("127.0.0.1", "12345", &hints, &result);

	SOCKET sock = INVALID_SOCKET;
	for (auto ptr = result; ptr != NULL; ptr = ptr->ai_next)
	{
		sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (sock == INVALID_SOCKET)
		{
			std::cerr << "Failed to create socket\n";
			WSACleanup();
			return 1;
		}

		if (connect(sock, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == SOCKET_ERROR)
		{
			closesocket(sock);
			sock = INVALID_SOCKET;
			continue;
		}
		break;
	}

	freeaddrinfo(result);

	char buff[8]{};
	recv(sock, buff, 8, 0);
	std::cout << "recv: " << buff << '\n';

	const auto size = std::atoll(buff);
	std::vector<char> data(size);

	int recv_bytes{};
	do {
		auto rc = recv(sock, data.data() + recv_bytes, static_cast<int>(data.size()), 0);
		std::cout << "recv " << rc << " total=" << (recv_bytes += rc) << '\n';
	} while (recv_bytes != size);
	shutdown(sock, SD_RECEIVE);

	auto result_cnt = std::to_string(proceed(data));
	std::cout << "result: " << result_cnt << '\n';
	send(sock, result_cnt.c_str(), static_cast<int>(result_cnt.size()), 0);

	shutdown(sock, SD_SEND);
	closesocket(sock);
	WSACleanup();

	return 0;
}

int server(std::string arg)
{
	std::ifstream input(INPUT_FILEPATH, std::ios::in | std::ios::binary);
	if (!input.is_open())
	{
		std::cerr << "Failed to open file: path=" << INPUT_FILEPATH << '\n';
		return -1;
	}

	std::vector<char> data;
	{
		std::array<char, HEADER_SIZE> header;
		input.read(header.data(), header.size());

		const auto data_offset = *reinterpret_cast<uint32_t*>(&header[10]);
		const auto width = *reinterpret_cast<uint32_t*>(&header[18]);
		const auto height = *reinterpret_cast<uint32_t*>(&header[22]);
		std::cout << "data_offset=" << data_offset << ", width=" << width << ", height=" << height << '\n';

		data.resize(data_offset - HEADER_SIZE);
		input.read(data.data(), data.size());

		data.reserve(((width * 3 + 3) & (~3)) * height);

		std::vector<char> row_padded((width * 3 + 3) & (~3));
		for (size_t i{}; i < height; ++i)
		{
			input.read(row_padded.data(), row_padded.size());
			for (size_t j{}; j < width * 3; j += 3)
			{
				data.push_back(row_padded[j]);
				data.push_back(row_padded[j + 1]);
				data.push_back(row_padded[j + 2]);
			}
		}

		assert(data.size() % 3 == 0);
	}

	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	struct addrinfo* result, hints;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;
	getaddrinfo(NULL, "12345", &hints, &result);

	SOCKET listen_sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	bind(listen_sock, result->ai_addr, static_cast<int>(result->ai_addrlen));
	listen(listen_sock, SOMAXCONN);

	uint64_t total_cnt{};
	for (size_t i{}; i < CLIENTS_NUM; ++i)
	{
		STARTUPINFO sinfo;
		ZeroMemory(&sinfo, sizeof(sinfo));

		PROCESS_INFORMATION pinfo;
		ZeroMemory(&pinfo, sizeof(pinfo));

		if (!CreateProcess(NULL, (arg + " client").data(), 0, 0, FALSE, CREATE_NEW_CONSOLE, 0, 0, &sinfo, &pinfo))
		{
			std::cerr << "Failed to create process " << i << '\n';
			continue;
		}

		SOCKET client = accept(listen_sock, NULL, NULL);

		const auto supply = (data.size() % CLIENTS_NUM) / 3;
		const auto spl_tmp = CLIENTS_NUM - 1 - i;
		const auto cull_data_size = data.size() - supply * 3;
		const long end = data.size() - 1 - (i + 1) * cull_data_size / CLIENTS_NUM - 3 * (spl_tmp < supply ? (supply - spl_tmp) : 0);
		const long start = data.size() - 1 - i * cull_data_size / CLIENTS_NUM - 3 * (spl_tmp < supply ? (supply - spl_tmp - 1) : 0);
		auto size = std::to_string(start - end); size.resize(sizeof(uint64_t));
		std::cout << "Sending client " << i << " size=" << size << '\n';
		send(client, size.c_str(), static_cast<int>(size.size()), 0);

		int send_bytes{};
		do {
			auto sd = send(client, &data[end + 1 + send_bytes], static_cast<int>(start - end), 0);
			std::cout << "send " << sd << " total=" << (send_bytes += sd) << '\n';
		} while (send_bytes != (start - end));
		shutdown(client, SD_SEND);

		char buff[8]{};
		recv(client, buff, 8, 0);
		std::cout << "Recv client " << i << " buff=" << buff << '\n';
		total_cnt += std::atoll(buff);
		shutdown(client, SD_RECEIVE);
		closesocket(client);

		WaitForSingleObject(pinfo.hProcess, INFINITE);
		CloseHandle(pinfo.hThread);
		CloseHandle(pinfo.hProcess);
	}
	std::cout << "total_cnt: " << total_cnt << '\n';
	closesocket(listen_sock);
	WSACleanup();

	return 0;
}

uint64_t proceed(const std::vector<char>& data)
{
	uint64_t cnt{};
	for (size_t i{}; i < data.size(); i += 3)
		if (static_cast<size_t>(data[i]) * data[i + 1] * data[i + 2] < 1000)
			++cnt;

	return cnt;
}