#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h> 

static const std::filesystem::path INPUT_FILEPATH{ "img01.bmp" };
static constexpr auto HEADER_SIZE{ 54U };
static constexpr auto CLIENTS_NUM{ 5U };
static constexpr auto PORT{ 12345 };

int client();
int server();
uint64_t proceed(const std::vector<char>& data);

int main()
{
	return server();
}

int client()
{
	std::cout << "client: created\n";
	// sleep(1);

	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	servaddr.sin_port = htons(PORT);

	auto sock = socket(AF_INET, SOCK_STREAM, 0);
	while (1)
	{
		auto rc = connect(sock, (struct sockaddr*)&servaddr, sizeof(servaddr));
		std::cout << "client: connect rc=" << rc << " errno=" << errno << '\n';
		if (!rc)
			break;
	}
	std::cout << "client: connected\n";

	char buff[8];
	read(sock, buff, sizeof(buff));
	std::cout << "client: recv buff=" << buff << '\n';

	const auto size = std::atoll(buff);
	std::vector<char> data(size);

	int recv_bytes{};
	do {
		auto rc = read(sock, data.data() + recv_bytes, data.size());
		recv_bytes += rc;
		std::cout << "client: recv " << rc << " total=" << recv_bytes << '\n';
		if (!rc)
			break;
	} while (recv_bytes != size);

	auto result_cnt = std::to_string(proceed(data));
	std::cout << "client: result: " << result_cnt << '\n';
	write(sock, result_cnt.c_str(), result_cnt.size());

	close(sock);

	return 0;
}

int server()
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
	input.close();

	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(PORT);

	auto listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	const int enable = 1;
	if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
		perror("setsockopt(SO_REUSEADDR) failed");
	if (bind(listen_sock, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0)
	{
		perror("Bind failed: ");
		return 1;
	}
	assert(0 == listen(listen_sock, SOMAXCONN));

	uint64_t total_cnt{};
	for (size_t i{}; i < CLIENTS_NUM; ++i)
	{
		struct sockaddr_in clientaddr;
		socklen_t len = sizeof((struct sockaddr*)&clientaddr);
		if (!fork())
		{
			close(listen_sock);
			return client();
		}

		int client{};
		do
		{
			client = accept(listen_sock, (struct sockaddr*)&clientaddr, &len);
			std::cout << "server: accept rc=" << client << " errno=" << errno << '\n';
		} while (client < 0);
		std::cout << "server: client connected\n";

		const auto supply = (data.size() % CLIENTS_NUM) / 3;
		const auto spl_tmp = CLIENTS_NUM - 1 - i;
		const auto cull_data_size = data.size() - supply * 3;
		const long end = data.size() - 1 - (i + 1) * cull_data_size / CLIENTS_NUM - 3 * (spl_tmp < supply ? (supply - spl_tmp) : 0);
		const long start = data.size() - 1 - i * cull_data_size / CLIENTS_NUM - 3 * (spl_tmp < supply ? (supply - spl_tmp - 1) : 0);
		auto size = std::to_string(start - end); size.resize(sizeof(uint64_t));
		std::cout << "server: sending client " << i << " size=" << size << '\n';
		write(client, size.c_str(), size.size());

		int send_bytes{};
		do {
			auto sd = write(client, &data[end + 1 + send_bytes], static_cast<size_t>(start - end));
			send_bytes += sd;
			std::cout << "server: send " << sd << " total=" << send_bytes << '\n';
			if (!sd)
				break;
		} while (send_bytes != (start - end));

		char buff[8]{};
		read(client, buff, 8);
		std::cout << "server: recv from client " << i << " buff=" << buff << '\n';
		total_cnt += std::atoll(buff);
		close(client);
		wait(NULL);
	}
	std::cout << "6b: total_cnt: " << total_cnt << '\n';
	close(listen_sock);

	return 0;
}
//
uint64_t proceed(const std::vector<char>& data)
{
	uint64_t cnt{};
	for (size_t i{}; i < data.size(); i += 3)
		if (static_cast<size_t>(data[i]) * data[i + 1] * data[i + 2] < 1000)
			++cnt;

	return cnt;
}
