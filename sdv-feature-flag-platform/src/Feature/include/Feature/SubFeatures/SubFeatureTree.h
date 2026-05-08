#pragma once

#include "Feature/SubFeatures/SubFeatureModule.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sdv::feature::sub {

enum class NodeState : uint8_t { Loaded, Running, Stopped, Killed };

/**
 * Composite Pattern for in-process sub-features.
 *
 * - Leaves: real sub-capabilities (LOCATION, FCA, RANGE, ...)
 * - Composites: groups (optional) to build a tree, even if groups aren't externally flaggable.
 *
 * External flag contract is owned by parent Feature (Flag::isSubFeatureEnabled).
 * This tree only asks a predicate `isEnabled(key)` when dispatching to a leaf.
 */
class ISubFeatureNode {
public:
    virtual ~ISubFeatureNode() = default;
    virtual const char* key() const = 0;      // empty string => group node (always enabled)
    virtual const char* version() const = 0;  // informational

    virtual void Load() { state_ = NodeState::Loaded; }
    virtual void Start() { state_ = NodeState::Running; }
    virtual void Stop() { state_ = NodeState::Stopped; }
    virtual void Kill() { state_ = NodeState::Killed; }

    NodeState State() const { return state_; }

    virtual bool IsComposite() const { return false; }
    virtual void Add(std::unique_ptr<ISubFeatureNode>) {}

    // Dispatch hooks (called by parent Feature)
    virtual void OnTick(const std::function<bool(const char*)>& isEnabled) = 0;
    virtual void OnVehicleLocation(const std::function<bool(const char*)>& isEnabled, const std::string& msg) = 0;
    virtual void OnRoadInfo(const std::function<bool(const char*)>& isEnabled, const std::string& msg) = 0;

protected:
    NodeState state_{NodeState::Loaded};
};

class CompositeNode final : public ISubFeatureNode {
public:
    explicit CompositeNode(std::string label, std::string ver = "1.0")
        : label_(std::move(label)), ver_(std::move(ver)) {}

    const char* key() const override { return ""; }
    const char* version() const override { return ver_.c_str(); }
    bool IsComposite() const override { return true; }

    void Add(std::unique_ptr<ISubFeatureNode> child) override {
        if (!child) return;
        children_.push_back(std::move(child));
    }

    void Load() override {
        ISubFeatureNode::Load();
        for (auto& c : children_) c->Load();
    }
    void Start() override {
        ISubFeatureNode::Start();
        for (auto& c : children_) c->Start();
    }
    void Stop() override {
        for (auto& c : children_) c->Stop();
        ISubFeatureNode::Stop();
    }
    void Kill() override {
        for (auto& c : children_) c->Kill();
        ISubFeatureNode::Kill();
    }

    void OnTick(const std::function<bool(const char*)>& isEnabled) override {
        if (state_ == NodeState::Killed) return;
        for (auto& c : children_) c->OnTick(isEnabled);
    }
    void OnVehicleLocation(const std::function<bool(const char*)>& isEnabled, const std::string& msg) override {
        if (state_ == NodeState::Killed) return;
        for (auto& c : children_) c->OnVehicleLocation(isEnabled, msg);
    }
    void OnRoadInfo(const std::function<bool(const char*)>& isEnabled, const std::string& msg) override {
        if (state_ == NodeState::Killed) return;
        for (auto& c : children_) c->OnRoadInfo(isEnabled, msg);
    }

private:
    std::string label_;
    std::string ver_;
    std::vector<std::unique_ptr<ISubFeatureNode>> children_;
};

class LeafNode final : public ISubFeatureNode {
public:
    LeafNode(std::unique_ptr<ISubFeatureModule> module, std::string ver = "1.0")
        : module_(std::move(module)), ver_(std::move(ver)) {}

    const char* key() const override { return module_ ? module_->key() : ""; }
    const char* version() const override { return ver_.c_str(); }

    void OnTick(const std::function<bool(const char*)>& isEnabled) override {
        if (state_ == NodeState::Killed || !module_) return;
        const char* k = module_->key();
        if (k && k[0] != '\0' && !isEnabled(k)) return;
        module_->onTick();
    }

    void OnVehicleLocation(const std::function<bool(const char*)>& isEnabled, const std::string& msg) override {
        if (state_ == NodeState::Killed || !module_) return;
        const char* k = module_->key();
        if (k && k[0] != '\0' && !isEnabled(k)) return;
        module_->onVehicleLocation(msg);
    }

    void OnRoadInfo(const std::function<bool(const char*)>& isEnabled, const std::string& msg) override {
        if (state_ == NodeState::Killed || !module_) return;
        const char* k = module_->key();
        if (k && k[0] != '\0' && !isEnabled(k)) return;
        module_->onRoadInfo(msg);
    }

private:
    std::unique_ptr<ISubFeatureModule> module_;
    std::string ver_;
};

}  // namespace sdv::feature::sub

