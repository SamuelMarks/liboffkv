#include <utility>

#pragma once

#include <string>


#include "operation.hpp"



struct Result {
    uint64_t version = 0;
};


struct CreateResult : Result {
};


struct SetResult : Result {
};


struct ExistsResult : Result {
    bool exists;
    std::unique_ptr<std::future<void>> watch;

    explicit operator bool() const
    {
        return exists;
    }

    bool operator!() const
    {
        return !exists;
    }
};


struct GetResult : Result {
    std::string value;
    std::unique_ptr<std::future<void>> watch;
};


struct CASResult : Result {
    bool success;

    explicit operator bool() const
    {
        return success;
    }

    bool operator!() const
    {
        return !success;
    }
};


class TransactionResult {
private:
    struct OperationResult {
        op::op_type type;
        std::shared_ptr<Result> result;

        OperationResult(op::op_type type, std::shared_ptr<Result> result)
            : type(type), result(std::move(result))
        {}
    };


    std::vector<OperationResult> op_results_;

public:
    TransactionResult()
    {}

    TransactionResult(std::vector<OperationResult>&& res)
        : op_results_(std::move(res))
    {}

    TransactionResult(const std::vector<OperationResult>& res)
        : op_results_(res)
    {}

    TransactionResult(const TransactionResult&) = default;

    TransactionResult(TransactionResult&&) = default;

    ~TransactionResult()
    {}


    template <typename U>
    void push_back(U&& res)
    {
        op_results_.push_back(std::forward<U>(res));
    }

    template <typename... Args>
    void emplace_back(Args&& ... args)
    {
        op_results_.emplace_back(std::forward<Args>(args)...);
    }

    void pop_back()
    {
        op_results_.pop_back();
    }

    auto begin() const
    {
        return op_results_.begin();
    }

    auto end() const
    {
        return op_results_.end();
    }
};
