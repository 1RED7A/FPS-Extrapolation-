#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// ─────────────────────────────────────────────────────────────────────────────
// Section 4 & 8 constants
// ─────────────────────────────────────────────────────────────────────────────
static constexpr float TELEPORT_THRESHOLD    = 300.f;
static constexpr float SPEED_CHANGE_THRESHOLD =   5.f;

// ─────────────────────────────────────────────────────────────────────────────
// ExtrapolationFields — all state for one game-layer instance
// ─────────────────────────────────────────────────────────────────────────────
struct ExtrapolationFields {
    // Timing
    float timeTilNextTick      = 0.f;
    float progressTilNextTick  = 0.f;
    float modifiedDeltaReturn  = 0.f;

    // Camera (Section 2, 8)
    CCPoint lastCamPos         = {};
    CCPoint lastCamPos2        = {};
    CCPoint lastCamDelta       = {};   // Section 8

    // Ground layers (Section 4)
    CCPoint lastGroundPos      = {};
    CCPoint lastGroundPos2     = {};
    CCPoint lastGround2Pos     = {};
    CCPoint lastGround2Pos2    = {};

    // Player rotation (Section 1, 7)
    float lastRot1             = 0.f;
    float lastRot2             = 0.f;

    // Tick detection (Section 3)
    int lastUpdateFrame        = -1;
};

// ─────────────────────────────────────────────────────────────────────────────
// GJBaseGameLayer modification
// ─────────────────────────────────────────────────────────────────────────────
class $modify(ExtrapolatedGameLayer, GJBaseGameLayer) {
    struct Fields {
        ExtrapolationFields ex = {};
    };

    // ── Section 2: helper that zeros every saved state field ─────────────────
    void resetExtrapolationState() {
        auto& ex = m_fields->ex;
        ex.timeTilNextTick     = 0.f;
        ex.progressTilNextTick = 0.f;
        ex.modifiedDeltaReturn = 0.f;
        ex.lastCamPos          = {};
        ex.lastCamPos2         = {};
        ex.lastCamDelta        = {};
        ex.lastGroundPos       = {};
        ex.lastGroundPos2      = {};
        ex.lastGround2Pos      = {};
        ex.lastGround2Pos2     = {};
        ex.lastRot1            = 0.f;
        ex.lastRot2            = 0.f;
        ex.lastUpdateFrame     = -1;
    }

    // ── Section 3: capture modifiedDelta before update() sees it ─────────────
    float getModifiedDelta(float dt) {
        float pRet = GJBaseGameLayer::getModifiedDelta(dt);
        m_fields->ex.modifiedDeltaReturn = pRet;
        return pRet;
    }

    // ── Section 2: hook resetLevel (death / restart) ──────────────────────────
    void resetLevel() {
        resetExtrapolationState();
        GJBaseGameLayer::resetLevel();
    }

    // ── Section 2: hook startGame (level begins / checkpoint) ────────────────
    void startGame() {
        resetExtrapolationState();
        GJBaseGameLayer::startGame();
    }

    // ── Main update ───────────────────────────────────────────────────────────
    void update(float dt) {
        GJBaseGameLayer::update(dt);

        // Master enable guard
        if (!Mod::get()->getSettingValue<bool>("enabled"))
            return;

        auto pl = PlayLayer::get();
        if (!pl) return;
        if (pl->m_levelEndAnimationStarted) return;
        if (!isRunning() || dt == 0.f) return;

        // Section 6: practice-mode guard
        if (pl->m_isPracticeMode &&
            Mod::get()->getSettingValue<bool>("disable-in-practice"))
            return;

        auto& ex = m_fields->ex;

        // Section 3: frame counter for hardened tick detection
        static int frameCounter = 0;
        frameCounter++;

        bool tickFired = (ex.modifiedDeltaReturn != 0.f) ||
                         (frameCounter != ex.lastUpdateFrame + 1);

        if (tickFired) {
            // ── Record ground positions BEFORE we might skip (Section 4) ────
            if (m_groundLayer)  {
                ex.lastGround2Pos2 = ex.lastGround2Pos;   // shift history
                ex.lastGround2Pos  = ex.lastGroundPos;    // (re-used below)
                ex.lastGroundPos2  = ex.lastGroundPos;
                ex.lastGroundPos   = m_groundLayer->getPosition();
            }
            if (m_groundLayer2) {
                ex.lastGround2Pos2 = ex.lastGround2Pos;
                ex.lastGround2Pos  = m_groundLayer2->getPosition();
            }

            // ── Camera delta & speed-change detection (Section 8) ───────────
            CCPoint currentDelta = ex.lastCamPos - ex.lastCamPos2;
            float   deltaChange  = ccpDistance(currentDelta, ex.lastCamDelta);
            if (deltaChange > SPEED_CHANGE_THRESHOLD) {
                // Collapse history so endPos == lastPos (one-frame no-extrap)
                ex.lastCamPos2      = ex.lastCamPos;
                ex.lastGroundPos2   = ex.lastGroundPos;
                ex.lastGround2Pos2  = ex.lastGround2Pos;
            }
            ex.lastCamDelta = currentDelta;

            // ── Update camera history ────────────────────────────────────────
            ex.timeTilNextTick    = ex.modifiedDeltaReturn;
            ex.progressTilNextTick = 0.f;
            ex.lastCamPos2        = ex.lastCamPos;
            ex.lastCamPos         = m_objectLayer->getPosition();

            // ── Save player rotations at tick time (Section 1) ───────────────
            if (m_player1 && m_player1->m_mainLayer)
                ex.lastRot1 = m_player1->m_mainLayer->getRotation();
            if (m_player2 && m_player2->m_mainLayer)
                ex.lastRot2 = m_player2->m_mainLayer->getRotation();

            ex.lastUpdateFrame = frameCounter;
        } else {
            ex.progressTilNextTick += dt;
        }

        if (ex.timeTilNextTick == 0.f) return;

        // Section 2: teleport detection — large camera jump means instant reset
        float camJump = ccpDistance(m_objectLayer->getPosition(), ex.lastCamPos);
        if (camJump > TELEPORT_THRESHOLD) {
            resetExtrapolationState();
            return;
        }

        // Section 5: clamped percent
        float maxPercent = Mod::get()->getSettingValue<float>("max-percent");
        float percent = std::clamp(
            ex.progressTilNextTick / ex.timeTilNextTick,
            0.f,
            maxPercent
        );

        // Section 6: per-component toggles
        bool doCamera = Mod::get()->getSettingValue<bool>("extrapolate-camera");
        bool doPlayer = Mod::get()->getSettingValue<bool>("extrapolate-player");
        bool doGround = Mod::get()->getSettingValue<bool>("extrapolate-ground");

        // ── Camera extrapolation ──────────────────────────────────────────────
        if (doCamera) {
            CCPoint endCamPos = ex.lastCamPos + (ex.lastCamPos - ex.lastCamPos2);
            m_objectLayer->setPosition({
                std::lerp(ex.lastCamPos.x, endCamPos.x, percent),
                std::lerp(ex.lastCamPos.y, endCamPos.y, percent)
            });
        }

        // ── Ground extrapolation (Section 4) ─────────────────────────────────
        if (doGround) {
            extrapolateGround(m_groundLayer,
                              ex.lastGroundPos,  ex.lastGroundPos2,  percent);
            extrapolateGround(m_groundLayer2,
                              ex.lastGround2Pos, ex.lastGround2Pos2, percent);
        }

        // ── Player extrapolation (Section 1, 7) ──────────────────────────────
        if (doPlayer) {
            extrapolatePlayer(m_player1, percent, ex.lastRot1);
            if (m_player2) extrapolatePlayer(m_player2, percent, ex.lastRot2);
        }
    }

    // ── Section 1 + 7: player extrapolation with correct rotation lerp ────────
    void extrapolatePlayer(PlayerObject* player, float percent, float& lastRot) {
        if (!player) return;

        // Position
        float endX = player->m_position.x +
                     (player->m_position.x - player->m_lastPosition.x);
        float endY = player->m_position.y +
                     (player->m_position.y - player->m_lastPosition.y);

        player->CCNode::setPosition({
            std::lerp(player->m_position.x, endX, percent),
            std::lerp(player->m_position.y, endY, percent)
        });

        // Rotation — lerp from lastRot to lastRot + rotDelta (Section 1)
        float rotSpeed = (player->m_isBall && player->m_isBallRotating)
            ? 1.f
            : player->m_rotateSpeed;
        float rotDelta    = (player->m_rotationSpeed * rotSpeed) / 240.f;
        float sidewaysOff = player->m_isSideways ? -90.f : 0.f;

        player->m_mainLayer->setRotation(
            std::lerp(lastRot, lastRot + rotDelta, percent) + sidewaysOff
        );
    }

    // ── Section 4: ground extrapolation using saved positions ─────────────────
    void extrapolateGround(GJGroundLayer* ground,
                           CCPoint lastPos, CCPoint lastPos2,
                           float percent) {
        if (!ground) return;
        CCPoint endPos = lastPos + (lastPos - lastPos2);
        ground->setPosition({
            std::lerp(lastPos.x, endPos.x, percent),
            std::lerp(lastPos.y, endPos.y, percent)
        });
        // Section 9: we move only the ground layer node itself —
        // never iterate children, never touch particles/effects.
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PlayLayer modification — Section 2: hook levelComplete
// ─────────────────────────────────────────────────────────────────────────────
class $modify(ExtrapolatedPlayLayer, PlayLayer) {
    void levelComplete() {
        // Reset extrapolation state so the end-screen has no stale camera pos
        if (auto* gl = static_cast<ExtrapolatedGameLayer*>(
                static_cast<GJBaseGameLayer*>(this))) {
            gl->resetExtrapolationState();
        }
        PlayLayer::levelComplete();
    }
};
