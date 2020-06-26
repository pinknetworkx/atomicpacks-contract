#include <atomicpacks.hpp>

#include "ram_handling.cpp"
#include "pack_creation.cpp"
#include "unboxing.cpp"


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