#pragma once

#include "storage.h"

namespace serialization {

class Serializer {
public:
  virtual void serialize(Snapshot &snapshot) = 0;
  virtual void deserialize(const Snapshot &snapshot) = 0;
};

}