#pragma once
#include <concepts>
#include <optional>
#include <vector>

template <class T>
concept Driver = requires(const T& driver) {
                     { driver.get_real_path("/tmp/image.jpg") } -> std::same_as<std::optional<std::string>>;                  // "/tmp/image.jxl"
                     { driver.get_phantom_paths("/tmp/image.jxl") } -> std::same_as<std::optional<std::vector<std::string>>>; // ["/tmp/image.jpg", "/tmp/image.png"]
                     { driver.open_phantom_file("/tmp/image.jpg") } -> std::same_as<std::optional<int>>;
                 };
