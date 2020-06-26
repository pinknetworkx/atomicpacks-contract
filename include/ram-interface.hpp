#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>

using namespace eosio;

namespace ram {

    static const symbol RAMCORE_SYMBOL = symbol("RAMCORE", 4);

    struct connector {
        asset balance;
        double weight = .5;
    };

    struct rammarket_s {
        asset supply;
        connector base;
        connector quote;

        auto primary_key() const { return supply.symbol.raw(); }
    };
    typedef eosio::multi_index<name("rammarket"), rammarket_s> rammarket_t;

    rammarket_t rammarket = rammarket_t(name("eosio"), name("eosio").value);


    //From exchange_state.cpp in eosio.system contract source
    int64_t get_bancor_output(
        int64_t inp_reserve,
        int64_t out_reserve,
        int64_t inp
    ) {
        const double ib = inp_reserve;
        const double ob = out_reserve;
        const double in = inp;

        int64_t out = int64_t( (in * ob) / (ib + in) );

        if ( out < 0 ) out = 0;

        return out;
    }


    int64_t get_purchase_ram_bytes(const asset &purchase_quantity) {
        auto itr = rammarket.find(RAMCORE_SYMBOL.raw());
        const int64_t ram_reserve = itr->base.balance.amount;
        const int64_t core_reserve = itr->quote.balance.amount;

        int64_t fee_amount = (purchase_quantity.amount + 199) / 200;
        int64_t purchase_amount_after_fee = purchase_quantity.amount - fee_amount;

        return get_bancor_output(core_reserve, ram_reserve, purchase_amount_after_fee);
    }

    asset get_sell_ram_quantity(int64_t bytes_to_sell) {
        auto itr = rammarket.find(RAMCORE_SYMBOL.raw());
        const int64_t ram_reserve = itr->base.balance.amount;
        const int64_t core_reserve = itr->quote.balance.amount;

        int64_t amount = get_bancor_output(ram_reserve, core_reserve, bytes_to_sell);
        return asset(amount, itr->quote.balance.symbol);
    }


}