#pragma once

#include <vector>
#include <string>
#include <optional>
#include <algorithm>

class ArgumentParser {
public:
  ArgumentParser(const int &argc, char **argv)
  {
    std::copy(argv, argv + argc, std::back_inserter(args));
  }

  std::optional<std::string> get_string(const std::string_view &option_name) const
  {
    auto it = std::find(args.begin(), args.end(), option_name);
    if (; it != args.end() && std::next(it) != args.end()) {
      return std::string(*std::next(it));
    }
    return std::optional<std::string>();
  }

  std::optional<bool> get_flag(const std::string_view &option_name) const
  {
    auto it = std::find(args.begin(), args.end(), option_name);
    if (it != args.end()) {
      return true;
    }
    return std::optional<bool>();
  }

private:
  std::vector<std::string_view> args;
};
