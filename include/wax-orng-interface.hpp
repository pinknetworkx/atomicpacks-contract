#include <eosio/eosio.hpp>

using namespace eosio;

namespace orng {

    static constexpr name ORNG_CONTRACT = name("orng.wax");

    TABLE signvals_a {
        uint64_t signing_value;

        auto primary_key() const { return signing_value; }
    };
    typedef multi_index <name("signvals.a"), signvals_a> signvals_t;
    
    signvals_t signvals = signvals_t(ORNG_CONTRACT, ORNG_CONTRACT.value);
}