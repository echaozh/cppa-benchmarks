/******************************************************************************\
 *           ___        __                                                    *
 *          /\_ \    __/\ \                                                   *
 *          \//\ \  /\_\ \ \____    ___   _____   _____      __               *
 *            \ \ \ \/\ \ \ '__`\  /'___\/\ '__`\/\ '__`\  /'__`\             *
 *             \_\ \_\ \ \ \ \L\ \/\ \__/\ \ \L\ \ \ \L\ \/\ \L\.\_           *
 *             /\____\\ \_\ \_,__/\ \____\\ \ ,__/\ \ ,__/\ \__/.\_\          *
 *             \/____/ \/_/\/___/  \/____/ \ \ \/  \ \ \/  \/__/\/_/          *
 *                                          \ \_\   \ \_\                     *
 *                                           \/_/    \/_/                     *
 *                                                                            *
 * Copyright (C) 2011-2013                                                    *
 * Dominik Charousset <dominik.charousset@haw-hamburg.de>                     *
 *                                                                            *
 * This file is part of libcppa.                                              *
 * libcppa is free software: you can redistribute it and/or modify it under   *
 * the terms of the GNU Lesser General Public License as published by the     *
 * Free Software Foundation, either version 3 of the License                  *
 * or (at your option) any later version.                                     *
 *                                                                            *
 * libcppa is distributed in the hope that it will be useful,                 *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                       *
 * See the GNU Lesser General Public License for more details.                *
 *                                                                            *
 * You should have received a copy of the GNU Lesser General Public License   *
 * along with libcppa. If not, see <http://www.gnu.org/licenses/>.            *
\******************************************************************************/


#include <chrono>
#include <utility>
#include <iostream>

#include "utility.hpp"

#include "cppa/cppa.hpp"

using namespace std;
using namespace cppa;

void usage() {
    cout << "Running in server mode:"                                    << endl
         << "  mode=server  "                                            << endl
         << "  --port=NUM       publishes an actor at port NUM"          << endl
         << "  -p NUM           alias for --port=NUM"                    << endl
         << endl
         << endl
         << "Running the benchmark:"                                     << endl
         << "  mode=benchmark run the benchmark, connect to any number"  << endl
         << "                 of given servers, use HOST:PORT syntax"    << endl
         << "  num_pings=NUM  run benchmark with NUM messages per node"  << endl
         << endl
         << "  example: mode=benchmark 192.168.9.1:1234 "
                                        "192.168.9.2:1234 "
                                        "--num_pings=100"                << endl
         << endl
         << endl
         << "Shutdown servers:"                                          << endl
         << "  mode=shutdown  shuts down any number of given servers"    << endl
         << endl
         << endl
         << "Miscellaneous:"                                             << endl
         << "  -h, --help       print this text and exit"                << endl
         << endl;
    exit(0);
}

template<class MatchExpr>
class actor_template {

    MatchExpr m_expr;

 public:

    actor_template(MatchExpr me) : m_expr(move(me)) { }

    actor_ptr spawn() const {
        struct impl : sb_actor<impl> {
            behavior init_state;
            impl(const MatchExpr& mx) : init_state(mx.as_partial_function()) {
            }
        };
        return cppa::spawn(new impl{m_expr});
    }

};

template<typename... Args>
auto actor_prototype(const Args&... args) -> actor_template<decltype(mexpr_concat(args...))> {
    return {mexpr_concat(args...)};
}

struct ping_actor : sb_actor<ping_actor> {

    behavior init_state;
    actor_ptr parent;

    ping_actor(actor_ptr parent_ptr) : parent(move(parent_ptr)) {
        init_state = (
            on(atom("kickoff"), arg_match) >> [=](actor_ptr pong, uint32_t value) {
                send(pong, atom("ping"), value);
                become (
                    on(atom("pong"), uint32_t(0)) >> [=] {
                        send(parent, atom("done"));
                        quit();
                    },
                    on(atom("pong"), arg_match) >> [=](uint32_t value) {
                        return make_cow_tuple(atom("ping"), value - 1);
                    },
                    others() >> [=] {
                        cout << "ping_actor: unexpected: "
                             << to_string(last_dequeued())
                             << endl;
                    }
                );
            },
            others() >> [=] {
                cout << "ping_actor: unexpected: "
                     << to_string(last_dequeued())
                     << endl;
            }
        );
    }

};

struct server_actor : sb_actor<server_actor> {

    typedef map<pair<string, uint16_t>, actor_ptr> pong_map;

    behavior init_state;
    pong_map m_pongs;

    server_actor() {
        trap_exit(true);
        init_state = (
            on(atom("ping"), arg_match) >> [=](uint32_t value) {
                return make_cow_tuple(atom("pong"), value);
            },
            on(atom("add_pong"), arg_match) >> [=](const string& host, uint16_t port) -> any_tuple {
                auto key = make_pair(host, port);
                auto i = m_pongs.find(key);
                if (i == m_pongs.end()) {
                    try {
                        auto p = remote_actor(host.c_str(), port);
                        link_to(p);
                        m_pongs.insert(make_pair(key, p));
                        return atom("ok");
                    }
                    catch (exception& e) {
                        return make_cow_tuple(atom("error"), e.what());
                    }
                }
                else {
                    return atom("ok");
                }
            },
            on(atom("kickoff"), arg_match) >> [=](uint32_t num_pings) {
                for (auto& kvp : m_pongs) {
                    auto ping = spawn<ping_actor>(last_sender());
                    send(ping, atom("kickoff"), kvp.second, num_pings);
                }
            },
            on(atom("purge")) >> [=] {
                m_pongs.clear();
            },
            on<atom("EXIT"), uint32_t>() >> [=] {
                actor_ptr who = last_sender();
                auto i = find_if(m_pongs.begin(), m_pongs.end(),
                                      [&](const pong_map::value_type& kvp) {
                    return kvp.second == who;
                });
                if (i != m_pongs.end()) m_pongs.erase(i);
            },
            on(atom("shutdown")) >> [=] {
                m_pongs.clear();
                quit();
            },
            others() >> [=] {
                cout << "unexpected: " << to_string(last_dequeued()) << endl;
            }

        );
    }

};

template<typename Arg0>
void usage(Arg0&& arg0) {
    cout << forward<Arg0>(arg0) << endl << endl;
    usage();
}

template<typename Arg0, typename Arg1, typename... Args>
void usage(Arg0&& arg0, Arg1&& arg1, Args&&... args) {
    cout << forward<Arg0>(arg0);
    usage(forward<Arg1>(arg1), forward<Args>(args)...);
}

template<typename Iterator>
void server_mode(Iterator first, Iterator last) {
    string port_prefix = "--port=";
    // extracts port from a key-value pair
    auto kvp_port = [&](const string& str) -> optional<int> {
        if (equal(port_prefix.begin(), port_prefix.end(), str.begin())) {
            return spro<int>(str.c_str() + port_prefix.size());
        }
        return {};
    };
    match(vector<string>{first, last}) ( (on(kvp_port) || on("-p", spro<int>)) >> [](int port) {
            if (port > 1024 && port < 65536) {
                publish(spawn<server_actor>(), port);
            }
            else {
                usage("illegal port: ", port);
            }
        },
        others() >> [=] {
            if (first != last) usage("illegal argument: ", *first);
            else usage();
        }
    );
}

template<typename Iterator>
void client_mode(Iterator first, Iterator last) {
    if (first == last) usage("no server, no fun");
    uint32_t init_value = 0;
    vector<pair<string, uint16_t> > remotes;
    string pings_prefix = "--num_pings=";
    auto num_msgs = [&](const string& str) -> optional<int> {
        if (equal(pings_prefix.begin(), pings_prefix.end(), str.begin())) {
            return spro<int>(str.c_str() + pings_prefix.size());
        }
        return {};
    };
    match_each(first, last, bind(split, std::placeholders::_1, ':')) (
        on(val<string>, spro<int>) >> [&](string& host, int port) {
            if (port <= 1024 || port >= 65536) {
                throw invalid_argument("illegal port: " + to_string(port));
            }
            remotes.emplace_back(move(host), static_cast<uint16_t>(port));
        },
        on(num_msgs) >> [&](int num) {
            if (num > 0) init_value = static_cast<uint32_t>(num);
        }
    );
    if (init_value == 0) {
        cout << "no non-zero, non-negative init value given" << endl;
        exit(1);
    }
    if (remotes.size() < 2) {
        cout << "less than two nodes given" << endl;
        exit(1);
    }
    vector<actor_ptr> remote_actors;
    for (auto& r : remotes) {
        remote_actors.push_back(remote_actor(r.first.c_str(), r.second));
    }
    // setup phase
    //cout << "tell server nodes to connect to each other" << endl;
    for (size_t i = 0; i < remotes.size(); ++i) {
        for (size_t j = 0; j < remotes.size(); ++j) {
            if (i != j) {
                auto& r = remotes[j];
                send(remote_actors[i], atom("add_pong"), r.first, r.second);
            }
        }
    }
    { // collect {ok} messages
        size_t i = 0;
        size_t end = remote_actors.size() * (remote_actors.size() - 1);
        receive_for(i, end) (
            on(atom("ok")) >> [] {
            },
            on(atom("error"), arg_match) >> [&](const string& str) {
                cout << "error: " << str << endl;
                for (auto& x : remote_actors) {
                    send(x, atom("purge"));
                }
                throw logic_error("");
            },
            others() >> [] {
                cout << "expected {ok|error}, received: "
                     << to_string(self->last_dequeued())
                     << endl;
                throw logic_error("");
            },
            after(chrono::seconds(10)) >> [&] {
                cout << "remote didn't answer within 10sec." << endl;
                for (auto& x : remote_actors) {
                    send(x, atom("purge"));
                }
                throw logic_error("");
            }
        );
    }
    // kickoff
    //cout << "setup done" << endl;
    //cout << "kickoff, init value = " << init_value << endl;
    for (auto& r : remote_actors) {
        send(r, atom("kickoff"), init_value);
    }
    { // collect {done} messages
        size_t i = 0;
        size_t end = remote_actors.size() * (remote_actors.size() - 1);
        receive_for(i, end) (
            on(atom("done")) >> [] {
                //cout << "...done..." << endl;
            },
            others() >> [] {
                cout << "unexpected: " << to_string(self->last_dequeued()) << endl;
                throw logic_error("");
            }
        );
    }
}

template<typename Iterator>
void shutdown_mode(Iterator first, Iterator last) {
    vector<pair<string, uint16_t> > remotes;
    match_each(first, last, bind(split, std::placeholders::_1, ':')) (
        on(val<string>, spro<int>) >> [&](string& host, int port) {
            if (port <= 1024 || port >= 65536) {
                throw invalid_argument("illegal port: " + to_string(port));
            }
            remotes.emplace_back(move(host), static_cast<uint16_t>(port));
        }
    );
    for (auto& r : remotes) {
        try {
            actor_ptr x = remote_actor(r.first, r.second);
            self->monitor(x);
            send(x, atom("shutdown"));
            receive (
                on(atom("DOWN"), val<uint32_t>) >> [] {
                    // ok, done
                },
                after(chrono::seconds(10)) >> [&] {
                    cerr << r.first << ":" << r.second << " didn't shut down "
                         << "within 10s"
                         << endl;
                }
            );
        }
        catch (std::exception& e) {
            cerr << "couldn't shutdown " << r.first << ":" << r.second
                 << "; reason: " << e.what()
                 << endl;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) usage();
    auto first = argv + 1;
    auto last = argv + argc;
    match(*first) (
        on("mode=server") >> [=] {
            server_mode(first + 1, last);
        },
        on("mode=benchmark") >> [=] {
            client_mode(first + 1, last);
            await_all_others_done();
        },
        on("mode=shutdown") >> [=] {
            shutdown_mode(first + 1, last);
        },
        (on("-h") || on("--help")) >> [] {
            usage();
        },
        others() >> [=] {
            usage("unknown argument: ", *first);
        }
    );
    await_all_others_done();
    shutdown();
}
