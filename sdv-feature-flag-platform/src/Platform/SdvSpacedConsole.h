#pragma once

#include <iostream>

// Inserts a blank line before each console log line (stdout/stderr).
#define SDV_SPC_COUT ((std::cout << '\n'), std::cout)
#define SDV_SPC_CERR ((std::cerr << '\n'), std::cerr)
