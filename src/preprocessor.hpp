#pragma once

#include "mdtc/compiler.hpp"

#include <string>
#include <string_view>

namespace mdtc {

[[nodiscard]] std::string preprocess(std::string_view source,
                                     const CompileOptions& options);

} // namespace mdtc
