#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ava::config {

struct ModelCapabilities {
  bool tool_call{false};
  bool vision{false};
  bool reasoning{false};
  bool streaming{false};
  bool loop_prone{false};
};

struct ModelLimits {
  std::size_t context_window{0};
  std::optional<std::size_t> max_output;
};

struct ModelCost {
  double input_per_million{0.0};
  double output_per_million{0.0};
  std::optional<double> cache_read_per_million;
  std::optional<double> cache_write_per_million;
};

struct RegisteredModel {
  std::string id;
  std::string provider;
  std::string name;
  std::vector<std::string> aliases;
  ModelCapabilities capabilities;
  ModelLimits limits;
  ModelCost cost;
};

class ModelRegistry {
 public:
  [[nodiscard]] static ModelRegistry load_embedded();

  [[nodiscard]] const RegisteredModel* find(const std::string& query) const;
  [[nodiscard]] const RegisteredModel* find_for_provider(
      const std::string& provider,
      const std::string& model
  ) const;
  [[nodiscard]] std::optional<std::pair<double, double>> pricing(const std::string& model) const;
  [[nodiscard]] std::vector<const RegisteredModel*> models_for_provider(const std::string& provider) const;
  [[nodiscard]] bool is_loop_prone(const std::string& model) const;
  [[nodiscard]] std::optional<std::string> normalize(const std::string& query) const;

  [[nodiscard]] const std::vector<RegisteredModel>& models() const { return models_; }

 private:
  std::vector<RegisteredModel> models_;
};

[[nodiscard]] const ModelRegistry& registry();

}  // namespace ava::config
