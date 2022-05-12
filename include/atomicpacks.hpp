#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/transaction.hpp>

#include <atomicassets-interface.hpp>
#include <ram-interface.hpp>
#include <wax-orng-interface.hpp>

using namespace std;
using namespace eosio;

static constexpr name   CORE_TOKEN_ACCOUNT = name("eosio.token");
static constexpr symbol CORE_TOKEN_SYMBOL  = symbol("WAX", 8);

CONTRACT atomicpacks : public contract {
public:
    using contract::contract;

    struct OUTCOME {
        uint32_t odds;
        int32_t  template_id; //-1 is equal to no NFT being minted
    };

    struct RAM_REFUND_DATA {
        name collection_name;
        uint64_t bytes;
    };


    ACTION setident(
        string contract_type,
        string version
    );

    ACTION retryrand(
        uint64_t pack_asset_id
    );
    

    ACTION announcepack(
        name authorized_account,
        name collection_name,
        uint32_t unlock_time,
        string display_data
    );

    ACTION addpackroll(
        name authorized_account,
        uint64_t pack_id,
        vector <OUTCOME> outcomes,
        uint32_t total_odds
    );

    ACTION delpackroll(
        name authorized_account,
        uint64_t pack_id,
        uint64_t roll_id
    );

    ACTION completepack(
        name authorized_account,
        uint64_t pack_id,
        int32_t pack_template_id
    );

    ACTION setpacktime(
        name authorized_account,
        uint64_t pack_id,
        uint32_t new_unlock_time
    );

    ACTION setpackdata(
        name authorized_account,
        uint64_t pack_id,
        string display_data
    );


    ACTION claimunboxed(
        uint64_t pack_asset_id,
        vector <uint64_t> origin_roll_ids
    );


    ACTION lognewpack(
        uint64_t pack_id,
        name collection_name,
        uint32_t unlock_time
    );

    ACTION lognewroll(
        uint64_t pack_id,
        uint64_t roll_id
    );

    ACTION logresult(
        uint64_t pack_asset_id,
        uint64_t pack_id,
        vector<int32_t> template_ids
    );


    ACTION withdrawram(
        name authorized_account,
        name collection_name,
        name recipient,
        int64_t bytes
    );

    ACTION refundram(
        name refund_type,
        uint64_t from_block,
        uint64_t to_block,
        vector<RAM_REFUND_DATA> ram_refund_data
    );

    ACTION buyramproxy(
        name collection_to_credit,
        asset quantity
    );


    ACTION receiverand(
        uint64_t assoc_id,
        checksum256 random_value
    );

    [[eosio::on_notify("eosio.token::transfer")]] void receive_token_transfer(
        name from,
        name to,
        asset quantity,
        string memo
    );

    [[eosio::on_notify("atomicassets::transfer")]] void receive_asset_transfer(
        name from,
        name to,
        vector <uint64_t> asset_ids,
        string memo
    );


private:

    TABLE packs_s {
        uint64_t pack_id;
        name     collection_name;
        uint32_t unlock_time;
        int32_t  pack_template_id  = -1; //-1 if the pack has not been activated yet
        uint64_t roll_counter = 0;
        string   display_data;

        uint64_t primary_key() const { return pack_id; }

        uint64_t by_template_id() const { return (uint64_t) pack_template_id; };
    };

    typedef multi_index<name("packs"), packs_s,
        indexed_by < name("templateid"), const_mem_fun < packs_s, uint64_t, &packs_s::by_template_id>>>
    packs_t;


    //Scope pack id
    TABLE packrolls_s {
        uint64_t         roll_id;
        vector <OUTCOME> outcomes;
        uint32_t         total_odds;

        uint64_t primary_key() const { return roll_id; }
    };

    typedef multi_index<name("packrolls"), packrolls_s> packrolls_t;


    TABLE unboxpacks_s {
        uint64_t pack_asset_id;
        uint64_t pack_id;
        name     unboxer;

        uint64_t primary_key() const { return pack_asset_id; }
        uint64_t by_unboxer() const { return unboxer.value; }
    };

    typedef multi_index<name("unboxpacks"), unboxpacks_s,
        indexed_by < name("unboxer"), const_mem_fun < unboxpacks_s, uint64_t, &unboxpacks_s::by_unboxer>>>
    unboxpacks_t;


    //Scope asset id of pack opened
    TABLE unboxassets_s {
        uint64_t origin_roll_id;
        int32_t  template_id;

        uint64_t primary_key() const { return origin_roll_id; }
    };

    typedef multi_index<name("unboxassets"), unboxassets_s> unboxassets_t;


    TABLE rambalances_s {
        name    collection_name;
        int64_t byte_balance;

        uint64_t primary_key() const { return collection_name.value; }
    };

    typedef multi_index<name("rambalances"), rambalances_s> rambalances_t;


    TABLE ramrefunds_s {
        name    refund_type;
        int64_t to_block;

        uint64_t primary_key() const { return refund_type.value; }
    };

    typedef multi_index<name("ramrefunds"), ramrefunds_s> ramrefunds_t;


    TABLE identifier_s {
        string contract_type = "atomicpacks";
        string version = "1.2.0";
    };
    typedef singleton <name("identifier"), identifier_s> identifier_t;
    // https://github.com/EOSIO/eosio.cdt/issues/280
    typedef multi_index <name("identifier"), identifier_s> identifier_t_for_abi;


    packs_t       packs       = packs_t(get_self(), get_self().value);
    unboxpacks_t  unboxpacks  = unboxpacks_t(get_self(), get_self().value);
    rambalances_t rambalances = rambalances_t(get_self(), get_self().value);
    ramrefunds_t  ramrefunds  = ramrefunds_t(get_self(), get_self().value);
    identifier_t  identifier  = identifier_t(get_self(), get_self().value);

    packrolls_t get_packrolls(uint64_t pack_id);

    unboxassets_t get_unboxassets(uint64_t pack_asset_id);


    void check_has_collection_auth(name account_to_check, name collection_name);


    //RAM Handlling
    void increase_ram_balance(name account, int64_t bytes);

    void decrease_ram_balance(name account, int64_t bytes, string error_message);

    void increase_collection_ram_balance(name collection_name, int64_t bytes);

    void decrease_collection_ram_balance(name collection_name, int64_t bytes, string error_message);
};
