#pragma once

#if __cplusplus < 202302L
    #error out of date c++ version, compile with -stdc++=2c
#elif defined(__clang__) && __clang_major__ < 19
    #error out of date clang, compile with latest version
#elif !defined(__clang__) && defined(__GNUC__) && __GNUC__ < 15
    #error out of date g++, compile with latest version
#elif defined(_MSC_VER) && _MSC_VER < 1943
    #error out of date msvc, compile with latest version
#else

#include <cctype>
#include <cinttypes>
#include <concepts>
#include <functional>
#include <optional>
#include <type_traits>
#include <unistd.h>
#include <utility>

#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>

namespace cel {
    enum class special_key : std::uint8_t {
        f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
        escape, tab, backspace, enter, left, up, right, down
    };
    struct key_flag {
        enum : std::uint8_t {
            ctrl = 1, alt = 1 << 1, shift = 1 << 2
        };
    };
    enum class result_code : std::uint8_t {
        success,
        no_bytes_read,
        failed_to_set_terminal_attributes,
        failed_to_reset_terminal_attributes,
        failed_to_setup_file_descriptor_pipeline,
        failed_on_waiting_for_event,
        failed_to_read_event
    };
    struct console_event_listener {
    private:
        std::array<std::int32_t, 2> m_fd_pipe;
        auto static set_terminal_attributes(auto... p_state) noexcept -> std::optional<termios> {
            if constexpr (sizeof...(p_state)) {
                tcsetattr(STDIN_FILENO, TCSANOW, std::addressof(p_state...[0]));
                return std::optional<termios>{p_state...[0]};                
            }
            else {
                auto l_termios = termios{};
                if (tcgetattr(STDIN_FILENO, std::addressof(l_termios)) != 0)
                    return std::optional<termios>{std::nullopt};;
                l_termios.c_lflag &= static_cast<decltype(l_termios.c_lflag)>(~(ICANON | ECHO));
                if (tcsetattr(STDIN_FILENO, TCSANOW, std::addressof(l_termios)) != 0)
                    return std::optional<termios>{std::nullopt};
                return std::optional<termios>{l_termios};
            }
        }
    protected:
        auto notify[[nodiscard]](std::uint8_t p_notificiation) const noexcept -> bool {
            return write(
                m_fd_pipe[1],
                static_cast<std::add_pointer_t<void>>(std::addressof(p_notificiation)),
                1
            ) != -1;
        }
        auto stop_listening[[nodiscard]]() const noexcept -> bool {
            return notify(std::uint8_t{0});
        }
        template <
            typename       tp_self_t,
            std::predicate tp_predicate_t
        >
        auto listen_while[[nodiscard]](
            this tp_self_t&& p_self,
            tp_predicate_t&& p_predicate
        )
        noexcept(noexcept(std::is_nothrow_invocable_v<tp_predicate_t>))
        -> result_code {
            auto constexpr static has_fully_defined_on_regular_key = requires(
                tp_self_t&&  p_object,
                char         p_key,
                std::uint8_t p_modifiers
            ) {
                std::forward<tp_self_t>(p_object).on_regular_key(
                    p_key,
                    p_modifiers
                );
            };
            auto constexpr static has_partially_defined_on_regular_key = requires(
                tp_self_t&&  p_object,
                char         p_key
            ) {
                std::forward<tp_self_t>(p_object).on_regular_key(p_key);
            };
            auto constexpr static has_fully_defined_on_special_key = requires(
                tp_self_t&&  p_object,
                special_key  p_key,
                std::uint8_t p_modifiers
            ) {
                std::forward<tp_self_t>(p_object).on_special_key(
                    p_key,
                    p_modifiers
                );
            };
            auto constexpr static has_partially_defined_on_special_key = requires(
                tp_self_t&&  p_object,
                special_key  p_key
            ) {
                std::forward<tp_self_t>(p_object).on_special_key(p_key);
            };
            auto constexpr static has_defined_on_notification = requires(
                tp_self_t&&  p_object,
                std::uint8_t p_notification
            ) {
                std::forward<tp_self_t>(p_object).on_notification(p_notification);
            };
            auto constexpr static has_defined_after_any_event_handled = requires(tp_self_t&& p_object) {
                std::forward<tp_self_t>(p_object).after_any_event_handled();
            };

            auto l_terminal_state = set_terminal_attributes();
            if (!l_terminal_state)
                return result_code::failed_to_set_terminal_attributes;
            if (pipe(std::ranges::data(p_self.m_fd_pipe)) == -1)
                return result_code::failed_to_setup_file_descriptor_pipeline;
            auto l_fd_poll = std::array<pollfd, 2>{
                pollfd{ .fd = STDIN_FILENO,        .events = POLLIN, .revents = {}},
                pollfd{ .fd = p_self.m_fd_pipe[0], .events = POLLIN, .revents = {}}
            };
            while (std::invoke(p_predicate)) {
                auto l_any_event_callback_invoked = false;
                if (poll(std::ranges::data(l_fd_poll), 2, -1) == -1)
                    return result_code::failed_on_waiting_for_event;
                if (l_fd_poll[1].revents & POLLIN) {
                    auto l_notification = std::uint8_t{0};
                    if (read(p_self.m_fd_pipe[0], std::addressof(l_notification), 1) == -1)
                        return result_code::failed_to_read_event;
                    if constexpr (has_defined_on_notification)
                        std::forward<tp_self_t>(p_self).on_notification(l_notification);
                    if (l_notification == 0)
                        break;
                }
                if (l_fd_poll[0].revents & POLLIN) {
                    auto l_key        = char{0};
                    auto l_flags      = std::uint8_t{0};
                    auto l_bytes      = std::array<char, 6>{};
                    auto const l_bytes_read = read(
                        STDIN_FILENO,
                        static_cast<void*>(std::ranges::data(l_bytes)),
                        std::ranges::size(l_bytes)
                    );
                    if (l_bytes_read == -1)
                        return result_code::failed_to_read_event;
                    if (!l_bytes_read)
                        return result_code::no_bytes_read;

                    auto on_regular_key = [&](auto p_key){
                        if constexpr (has_fully_defined_on_regular_key) {
                            std::forward<tp_self_t>(p_self).on_regular_key(
                                p_key,
                                l_flags
                            );
                            l_any_event_callback_invoked = true;
                        }
                        else if constexpr (has_partially_defined_on_regular_key) {
                            std::forward<tp_self_t>(p_self).on_regular_key(p_key);
                            l_any_event_callback_invoked = true;
                        }
                    };
                    auto on_special_key = [&](auto p_key){
                        if constexpr (has_fully_defined_on_special_key) {
                            std::forward<tp_self_t>(p_self).on_special_key(
                                p_key,
                                l_flags
                            );
                            l_any_event_callback_invoked = true;
                        }
                        else if constexpr (has_partially_defined_on_special_key) {
                            std::forward<tp_self_t>(p_self).on_special_key(p_key);
                            l_any_event_callback_invoked = true;
                        }
                    };
                    auto bitor_shift_flag_if_upper = [&]() {
                        if (std::isalpha(l_key) && std::toupper(l_key) == l_key)
                            l_flags |= key_flag::shift;
                    };

                    // note: special
                    if (l_bytes[0] == 9)
                        on_special_key(special_key::tab);
                    else if (l_bytes[0] == 10)
                        on_special_key(special_key::enter);
                    else if (l_bytes_read == 1 && l_bytes[0] == 27)
                        on_special_key(special_key::escape);
                    else if (l_bytes[0] == 127)
                        on_special_key(special_key::backspace);
                    // note; ctrl + a-z
                    else if (l_bytes[0] >= 0 && l_bytes[0] <= 26) {
                        l_key = 'a' + (l_bytes[0] - 1);
                        l_flags |= key_flag::ctrl;
                        bitor_shift_flag_if_upper();
                        on_regular_key(l_key);
                    }
                    // note: printable
                    else if (l_bytes[0] >= 32 && l_bytes[0] <= 126) {
                        l_key = l_bytes[0];
                        bitor_shift_flag_if_upper();
                        on_regular_key(l_key);
                    }
                    else if (l_bytes[0] == 27) {
                        // note: arrow keys, and hotkeys + arrow keys
                        if (l_bytes[1] == 91) {
                            if (l_bytes_read > 3 && l_bytes[2] == 49) {
                                switch (l_bytes[4]) {
                                    case 51: l_flags |= key_flag::alt;                                    break;
                                    case 52: l_flags |= key_flag::alt | key_flag::shift;                  break;
                                    case 53: l_flags |= key_flag::ctrl;                                   break;
                                    case 54: l_flags |= key_flag::ctrl | key_flag::shift;                 break;
                                    case 55: l_flags |= key_flag::ctrl | key_flag::alt;                   break;
                                    case 56: l_flags |= key_flag::ctrl | key_flag::alt | key_flag::shift; break;
                                }
                            }
                            switch (l_bytes[l_flags ? 5 : 2]) {
                                case 'A': on_special_key(special_key::up);    break;
                                case 'B': on_special_key(special_key::down);  break;
                                case 'C': on_special_key(special_key::right); break;
                                case 'D': on_special_key(special_key::left);  break;
                            }
                        }
                        // note: fkeys (1-4)
                        else if (l_bytes[1] == 79) {
                            switch (l_bytes[2]) {
                                case 'P': on_special_key(special_key::f1); break;
                                case 'Q': on_special_key(special_key::f2); break;
                                case 'R': on_special_key(special_key::f3); break;
                                case 'S': on_special_key(special_key::f4); break;
                            }
                        }
                        // note: ctrl + alt + a-z
                        else if (l_bytes[1] >= 0 && l_bytes[1] <= 26) {
                            l_key = 'a' + (l_bytes[1] - 1);
                            l_flags |= key_flag::ctrl | key_flag::alt;
                            on_regular_key(l_key);
                        }
                        // note: alt + printable
                        else if (l_bytes[1] >= 32 && l_bytes[1] <= 126) {
                            l_key = l_bytes[1];
                            l_flags |= key_flag::alt;
                            bitor_shift_flag_if_upper();
                            on_regular_key(l_key);
                        }
                    }
                }
                if constexpr (has_defined_after_any_event_handled)
                    if (l_any_event_callback_invoked)
                        std::forward<tp_self_t>(p_self).after_any_event_handled();
            }
            if (!set_terminal_attributes(*l_terminal_state))
                return result_code::failed_to_reset_terminal_attributes;
            return result_code::success;
        }
        template <typename tp_self_t>
        auto listen[[nodiscard]](this tp_self_t&& p_self) noexcept -> result_code {
            return std::forward<tp_self_t>(p_self).listen_while([]() noexcept { return true; });
        }
    };
}

#endif
