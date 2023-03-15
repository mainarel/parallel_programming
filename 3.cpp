#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

static const std::filesystem::path INPUT_FILEPATH{ "img03.bmp" };
static constexpr auto HEADER_SIZE{ 54U };
static constexpr auto THREADS_NUM{ 5U };

uint64_t proceed(const std::vector<char>& data);

int main()
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

    const auto res = proceed(data);
    std::cout << "3: cnt=" << res << '\n';

    return 0;
}

uint64_t proceed(const std::vector<char>& data)
{
    std::mutex m;
    uint64_t cnt{};

    std::vector<std::thread> workers;
    for (size_t i{}; i < THREADS_NUM; ++i)
        workers.emplace_back([&data, &m, &cnt, tid = i] {
        uint64_t local_cnt{};
        const long end = data.size() - (tid + 1) * data.size() / THREADS_NUM - 1;
        const long start = data.size() - tid * data.size() / THREADS_NUM - 1;
        assert((start - end) % 3 == 0);
        for (long i = start; i > end; i -= 3)
            if (static_cast<size_t>(data[i]) * data[i - 1] * data[i - 2] < 1000)
                ++local_cnt;

        std::lock_guard _(m);
        cnt += local_cnt;
        std::cout << std::this_thread::get_id() << "(" << tid << "): local_cnt=" << local_cnt << '\n';
            });

    for (auto&& worker : workers)
        worker.join();

    return cnt;
}
