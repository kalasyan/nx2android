// nx2android: Eden's boost flat map alias as ankerl::unordered_dense
#pragma once
#include <ankerl/unordered_dense.h>
namespace Common {
template <typename K, typename V, typename H = ankerl::unordered_dense::hash<K>, typename E = std::equal_to<K>>
using unordered_map = ankerl::unordered_dense::map<K, V, H, E>;
}
