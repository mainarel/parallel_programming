#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <omp.h>

static const std::filesystem::path INPUT_FILEPATH{ "img02.bmp" };
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
    std::cout << "4: cnt=" << res << '\n';

    return 0;
}

uint64_t proceed(const std::vector<char>& data)
{
    omp_set_dynamic(0);
    omp_set_num_threads(THREADS_NUM);

    uint64_t cnt{};
#pragma omp parallel for num_threads(THREADS_NUM) reduction(+:cnt)
    for (int i{}; i < data.size(); i += 3)
        if (static_cast<size_t>(data[i]) * data[i + 1] * data[i + 2] < 1000)
            ++cnt;

    return cnt;
}