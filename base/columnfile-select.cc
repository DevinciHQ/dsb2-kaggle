#include "base/columnfile.h"

namespace ev {

ColumnFileSelect::ColumnFileSelect(ColumnFileReader input)
    : input_(std::move(input)) {}

void ColumnFileSelect::AddSelection(uint32_t field) {
  selection_.emplace(field);
}

void ColumnFileSelect::AddFilter(
    uint32_t field, Delegate<bool(const StringRefOrNull&)> filter) {
  filters_.emplace_back(field, std::move(filter));
}

void ColumnFileSelect::Execute(
    ev::concurrency::RegionPool& region_pool,
    Delegate<void(const std::vector<std::pair<uint32_t, StringRefOrNull>>&)>
        callback) {
  if (filters_.empty()) {
    input_.SetColumnFilter(selection_.begin(), selection_.end());
    while (!input_.End()) callback(input_.GetRow());

    return;
  }

  // Sort filters by column index.
  std::stable_sort(
      filters_.begin(), filters_.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  struct RowCache {
    std::vector<std::pair<uint32_t, StringRefOrNull>> data;
    uint32_t index;
  };

  std::vector<RowCache> selected_rows;
  std::vector<std::pair<uint32_t, StringRefOrNull>> row_buffer;

  // Columns that appear in `selection_`, but not in `filters_`.
  std::unordered_set<uint32_t> unfiltered_selection = selection_;
  for (const auto& filter : filters_) unfiltered_selection.erase(filter.first);

  for (;;) {
    selected_rows.clear();

    // Used to hold temporary copies of strings.
    auto region = region_pool.GetRegion();

    size_t filter_idx = 0;

    // Iterate over all filters.
    do {
      auto field = filters_[filter_idx].first;

      input_.SetColumnFilter({field});
      if (filter_idx == 0) {
        if (input_.End()) return;
      } else {
        input_.SeekToStartOfSegment();
      }

      auto filter_selected = selection_.count(field);

      size_t filter_range_end = filter_idx + 1;

      while (filter_range_end != filters_.size() &&
             filters_[filter_range_end].first == field)
        ++filter_range_end;

      auto in = selected_rows.begin();
      auto out = selected_rows.begin();

      // Iterate over all values in the current segment for the current column.
      for (uint32_t row_idx = 0; !input_.EndOfSegment(); ++row_idx) {
        auto row = input_.GetRow();

        if (filter_idx > 0) {
          // Is row already filtered?
          if (row_idx < in->index) continue;

          KJ_ASSERT(in->index == row_idx);
        }

        ev::StringRefOrNull value = nullptr;
        if (row.size() == 1) {
          KJ_ASSERT(row[0].first == field, row[0].first, field);
          value = row[0].second;
        }

        bool match = true;

        for (size_t i = filter_idx; i != filter_range_end; ++i) {
          if (!filters_[i].second(value)) {
            match = false;
            break;
          }
        }

        if (match) {
          if (filter_idx == 0) {
            RowCache row_cache;
            row_cache.index = row_idx;

            if (filter_selected) {
              if (value.IsNull())
                row_cache.data.emplace_back(field, nullptr);
              else
                row_cache.data.emplace_back(field,
                                            value.StringRef().dup(region));
            }

            selected_rows.emplace_back(std::move(row_cache));
          } else {
            if (out != in) *out = std::move(*in);

            if (filter_selected) {
              if (value.IsNull())
                out->data.emplace_back(field, nullptr);
              else
                out->data.emplace_back(field, value.StringRef().dup(region));
            }

            ++out;
            ++in;
          }
        }
      }

      if (filter_idx > 0) selected_rows.erase(out, selected_rows.end());

      filter_idx = filter_range_end;
    } while (!selected_rows.empty() && filter_idx < filters_.size());

    if (selected_rows.empty()) continue;

    if (!unfiltered_selection.empty()) {
      input_.SetColumnFilter(unfiltered_selection.begin(),
                             unfiltered_selection.end());
      input_.SeekToStartOfSegment();

      auto sr = selected_rows.begin();

      for (uint32_t row_idx = 0; !input_.EndOfSegment(); ++row_idx) {
        if (sr == selected_rows.end()) break;

        auto row = input_.GetRow();

        if (row_idx < sr->index) continue;

        KJ_REQUIRE(row_idx == sr->index, row_idx, sr->index);

        for (const auto& d : row) {
          const auto& value = d.second;
          if (value.IsNull())
            sr->data.emplace_back(d.first, nullptr);
          else
            sr->data.emplace_back(d.first, d.second.StringRef().dup(region));
        }

        ++sr;
      }

      while (!input_.EndOfSegment()) input_.GetRow();
    }

    for (auto& row : selected_rows) {
      std::sort(row.data.begin(), row.data.end(),
                [](const auto& lhs, const auto& rhs) {
                  return lhs.first < rhs.first;
                });
      callback(row.data);
    }
  }
}

}  // namespace ev
