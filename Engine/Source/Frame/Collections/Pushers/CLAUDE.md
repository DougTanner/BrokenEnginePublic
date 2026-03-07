# /Engine/Source/Frame/Collections/Pushers/

Physics force fields with zone-based spatial acceleration for push queries with flag-based filtering. Compiles in both client and server builds using the Sync pattern.

## File Structure

The implementation is split across two `.cpp` files:
- **Pushers.cpp** - Registration, lifecycle (spawn/transfer/destroy), equality
- **PushersUpdate.cpp** - Update, sync, add/remove, zone setup, push application, collision phases

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
