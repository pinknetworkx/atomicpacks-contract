#include <atomicpacks.hpp>

#include "ram_handling.cpp"
#include "pack_creation.cpp"
#include "unboxing.cpp"


/**
* This function is called when the contract receives an eosio.token transfer
* Any core token transferred to the contract is automatically converted to RAM and added
* to the sender's RAM balance
*/
void atomicpacks::receive_token_transfer(
    name from,
    name to,
    asset quantity,
    string memo
) {
    const set <name> ignore = set < name > {
        // EOSIO system accounts
        name("eosio.stake"),
        name("eosio.names"),
        name("eosio.ram"),
        name("eosio.rex"),
        name("eosio")
    };

    if (to != get_self() || ignore.find(from) != ignore.end()) {
        return;
    }

    if (memo.find("deposit_collection_ram:") == 0) {
        check(get_first_receiver() == CORE_TOKEN_ACCOUNT && quantity.symbol == CORE_TOKEN_SYMBOL,
            "Must transfer core token when depositing RAM");

        name parsed_collection_name = name(memo.substr(23));

        atomicassets::collections.require_find(parsed_collection_name.value,
            ("No collection with this name exists: " + parsed_collection_name.to_string()).c_str());

        action(
            permission_level{get_self(), name("active")},
            get_self(),
            name("buyramproxy"),
            std::make_tuple(
                parsed_collection_name,
                quantity
            )
        ).send();
    } else {
        check(false, "invalid memo");
    }
}


/**
* Checks if the account_to_check is in the authorized_accounts vector of the specified collection
*/
void atomicpacks::check_has_collection_auth(
    name account_to_check,
    name collection_name
) {
    auto collection_itr = atomicassets::collections.require_find(collection_name.value,
        "No collection with this name exists");

    check(std::find(
        collection_itr->authorized_accounts.begin(),
        collection_itr->authorized_accounts.end(),
        account_to_check
        ) != collection_itr->authorized_accounts.end(),
        "The account " + account_to_check.to_string() + " is not authorized within the collection");
}


atomicpacks::packrolls_t atomicpacks::get_packrolls(uint64_t pack_id) {
    return packrolls_t(get_self(), pack_id);
}

atomicpacks::unboxassets_t atomicpacks::get_unboxassets(uint64_t pack_asset_id) {
    return unboxassets_t(get_self(), pack_asset_id);
}