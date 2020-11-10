#include <atomicpacks.hpp>

#include "ram_handling.cpp"
#include "pack_creation.cpp"
#include "unboxing.cpp"


/**
* Sets the identifier singleton
*
* @required_auth The contract itself
*/
ACTION atomicpacks::setident(
    string contract_type,
    string version
) {
    require_auth(get_self());

    identifier.set({.contract_type = contract_type, .version = version}, get_self());
}


/**
* Requests new randomness for a given assoc_id
* This is supposed to be used in the rare case that the RNG oracle kills a job for a pack unboxing
* due to issues with the finisher script.
*
* @required_auth The contract itself
*/
ACTION atomicpacks::retryrand(
    uint64_t pack_asset_id
) {
    require_auth(get_self());

    unboxpacks.require_find(pack_asset_id,
        "No open unboxpacks entry with the specified pack asset id exists");
    
    unboxassets_t unboxassets = get_unboxassets(pack_asset_id);
    check(unboxassets.begin() == unboxassets.end(),
        "The specified pack asset id already has results");

    //Get signing value from transaction id
    //As this is only used as the signing value for the randomness oracle, it does not matter that this
    //signing value is not truly random
    size_t size = transaction_size();
    char buf[size];
    uint32_t read = read_transaction(buf, size);
    check(size == read, "Signing value generation: read_transaction() has failed.");
    checksum256 tx_id = eosio::sha256(buf, read);
    uint64_t signing_value;
    memcpy(&signing_value, tx_id.data(), sizeof(signing_value));

    //Check if the signing_value was already used.
    //If that is the case, increment the signing_value until a non-used value is found
    while (orng::signvals.find(signing_value) != orng::signvals.end()) {
        signing_value++;
    }

    action(
        permission_level{get_self(), name("active")},
        orng::ORNG_CONTRACT,
        name("requestrand"),
        std::make_tuple(
            pack_asset_id,
            signing_value,
            get_self()
        )
    ).send();
}


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