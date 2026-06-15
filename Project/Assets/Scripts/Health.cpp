// ProjectUnity script asset metadata.
// Runtime implementation is currently registered in engine/scripting.

struct Health {
    float maxHealth = 100.0f;
    float currentHealth = 100.0f;
    float regenerationPerSecond = 0.0f;

    void onUpdate(auto& ctx, float deltaTime) {
        currentHealth = ctx.clamp(
            currentHealth + regenerationPerSecond * deltaTime,
            0.0f,
            maxHealth);
    }
};
