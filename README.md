
# atomicpacks

The atomicpacks smart contract allows users to set up random pack openings, giving out [AtomicAssets](https://github.com/pinknetworkx/atomicassets-contract) NFTs. It uses the [WAX RNG Oracle](https://github.com/worldwide-asset-exchange/wax-orng) to get randomness.

The smart contract is designed to be deployed only once and then used by different projects. It therefore has an internal RAM balance system that collections have to deposit RAM to, which is then used to pay for the RAM that is required when minting new NFTs.

## Setting up a pack

 1. Deposit RAM by transferring the chains core tokens (e.g. WAX on the WAX blockchain) to the atomicpacks contract with the memo `deposit_collection_ram:<collection name>`, e.g. `deposit_collection_ram:officialhero`. \
This will result in the atomicpacks contract using the tokens to buy RAM and crediting the received bytes to the collection in the `rambalances` table.

 2. Initialize the pack using the `announcepack` action. The authorized account needs to be an account that is authorized for the collection in AtomicAssets. The `unlock_time` parameter specifies the time in seconds since epoch after which the pack should become available. This allows restricting access to the opening until this specified time.

 3. Add rolls to the pack using the `addpackroll` action. Each roll represents one (or zero) NFTs to be given out, and each roll can have an individual set of outcomes. Outcomes each have odds and a template id. This template id will be used for minting the NFTs if the outcome is chosen randomly. This means that the templates have to be created before setting up the rolls. The template ids must either be templates of the collection that the pack belongs to, or `-1` to denote no NFT being minted. \
Outcomes have to be sorted in descending order based on their odds. \
Example values when using a library that accepts json inputs:
```json
{
  "authorized_account": "myaccount",
  "pack_id": 0,
  "outcomes": [
    {
      "odds": 50,
      "template_id": 100
    },
    {
      "odds": 30,
      "template_id": 101
    },
    {
      "odds": 20,
      "template_id": -1
    }
  ],
  "total_odds": 100
}
```

 4. After adding all rolls to the pack, finalize it using the `completepack` action. The `template_id` parameter of this action specifies the template id of the pack NFTs. Any NFT with that template id will be viewed as a pack by the atomicpacks contract. \
After calling the `completepack` action it is no longer possible to modify the rolls of the pack. It is however still possible to modify the unlock time and the description.

## Opening a pack

 1. Using the AtomicAssets transfer action, transfer a single pack NFT to the atomicpacks contract with the memo `unbox`. The atomicpacks contract will then call the WAX RNG oracle to request randomness.

 2. When receiving the callback from the WAX RNG oracle, the atomicpacks goes through all rolls of the pack that is being opened, and generates a random result for each based on the specified odds. \
The results are stored in the `unboxassets` table with the scope being the asset_id of the pack NFT that was opened. On top of that, an entry in the `unboxpacks` table is also made for the opened pack. \
No NFTs are minted at this stage, the results are the template ids of the selected outcomes.

3. The account that initially transferred the pack to the atomicpacks contract can now call the `claimunboxed` action to claim the results. The `origin_roll_ids` parameter is a vector of the origin roll ids that should be claimed (as they are used in the `unboxassets` table). Once a certain origin roll id is claimed, it is erased from the `unboxassets` table. Once all origin roll ids are claimed, the `unboxpacks` entry is also erased.

## Example frontend flow

 1. Let the user select the pack NFT that they want to open, and then transfer the NFT to the atomicpacks contract with the memo `unbox`
 
 2. Repeatedly poll the `unboxassets` table with the scope being the asset_id of the transferred pack NFT. This will initially return no result, until the callback from the WAX RNG oracle is executed. This should usually only take a few seconds. Once there is data in this table, that is the result of the pack opening.

3. Let the user call the `claimunboxed` action so that they receive the NFTs.

4. Display the result to the user, using the template ids that were found in the `unboxassets` table.

Note that step 3 and 4 could also be swapped. We however believe that it is better to let the user call the claim action first, to prevent situations in which the user might exit the process before actually calling the claim action.

Also note that the `unboxpacks` table has a secondary index called `unboxer`. This can be used to detect any unclaimed results that the user might still have. \
(Reminder: The `unboxpacks` entry is erased once all results of that entry are claimed, so if there still is an entry in this table, you know that there must be unclaimed results)
