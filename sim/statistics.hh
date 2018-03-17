/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __SIM_STATISTICS__
#define __SIM_STATISTICS__

#include <cinttypes>
#include <string>
#include <vector>

namespace SimpleSSD {

typedef struct {
  std::string name;
  std::string desc;
} Stats;

class StatObject {
 public:
  StatObject() {}
  virtual ~StatObject() {}

  virtual void getStatList(std::vector<Stats> &, std::string) {}
  virtual void getStatValues(std::vector<double> &) {}
  virtual void resetStatValues() {}
};

}  // namespace SimpleSSD

#endif
