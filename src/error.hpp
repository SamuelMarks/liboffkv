#pragma once

#include <exception>
#include <future>

#include <ppconsul/error.h>
#include <ppconsul/kv.h>
#include <zk/error.hpp>



class InvalidAddress : public std::exception {
public:
    virtual const char * what() const noexcept override
    {
        return "invalid address";
    }
};


class InvalidKey : public std::exception {
public:
    virtual const char * what() const noexcept override
    {
        return "invalid key";
    }
};


class NoEntry : public std::exception {
public:
    virtual const char* what() const noexcept override
    {
        return "no entry";
    }
};


class EntryExists : public std::exception {
    virtual const char* what() const noexcept override
    {
        return "entry exists";
    }
};



template <typename Func>
auto liboffkv_try(Func&& f)
{
    try {
        return std::forward<Func>(f)();
    } catch (zk::error& e) {
        switch (e.code()) {
            case zk::error_code::no_entry:
                throw NoEntry{};
            case zk::error_code::entry_exists:
                throw EntryExists{};
            case zk::error_code::version_mismatch:
                throw e;
            default: __builtin_unreachable();
        }
    } catch (ppconsul::BadStatus& e) {
        throw e;
    } catch (ppconsul::kv::UpdateError& e) {
        throw e;
    }
}


auto call_get = [](auto&& res) {
    return liboffkv_try([&res] {
        return res.get();
    });
};


auto call_get_then_ignore = [](auto&& res) {
    liboffkv_try([&res] {
        res.get();
    });
};
