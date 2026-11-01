{-|
Module      : Main
Description : M5Core2 集中監視モニタ・純粋関数型状態機械エンジン
Maintainer  : info@kuono.net

M5StickC Plus2（子機）から ESP-NOW 経由で送信される稼働パケットを受信し、
機器の稼働・停止・電波途絶状態の判別、デュアルバッテリ監視、および
3段階ミュート（通常／1回消音／永続停止）を統括する状態機械を提供します。
-}
module Main where

--------------------------------------------------------------------------------
-- ドメイン型定義
--------------------------------------------------------------------------------

data MachineStatus
  = StatusStopped
  | StatusRunning
  | StatusDisconnected
  deriving (Eq, Show)

statusToInt :: MachineStatus -> Int
statusToInt StatusStopped      = 0
statusToInt StatusRunning      = 1
statusToInt StatusDisconnected = 2

data MuteMode
  = ModeNormal
  | ModeMuteOnce
  | ModePersistent
  deriving (Eq, Show)

muteModeToInt :: MuteMode -> Int
muteModeToInt ModeNormal     = 0
muteModeToInt ModeMuteOnce   = 1
muteModeToInt ModePersistent = 2

type BuzzerCmd = Int
type Millis = Int
type DeltaG = Float

data MonitorContext = MonitorContext
  { currentStatus  :: !MachineStatus
  , muteMode       :: !MuteMode
  , lastPacketTime :: !Millis
  , lastDelta      :: !DeltaG
  , lastStickBat   :: !Int
  } deriving (Show)

--------------------------------------------------------------------------------
-- C 言語 FFI 境界インターフェース
--------------------------------------------------------------------------------

foreign import ccall "c_get_millis"         c_get_millis         :: IO Millis
foreign import ccall "c_poll_has_packet"    c_poll_has_packet    :: IO Int
foreign import ccall "c_get_packet_status"  c_get_packet_status  :: IO Int
foreign import ccall "c_get_packet_delta"   c_get_packet_delta   :: IO DeltaG
foreign import ccall "c_get_packet_battery" c_get_packet_battery :: IO Int
foreign import ccall "c_get_core2_battery"  c_get_core2_battery  :: IO Int
foreign import ccall "c_poll_btn_b_pressed" c_poll_btn_b_pressed :: IO Int

-- C++ 側のテンプレートエンジンへ数値を注入 (CString 不要)
foreign import ccall "c_render_template"    c_render_template    :: Int -> Int -> DeltaG -> Int -> Int -> IO ()
foreign import ccall "c_set_buzzer"         c_set_buzzer         :: BuzzerCmd -> IO ()
foreign import ccall "c_delay_ms"           c_delay_ms           :: Millis -> IO ()

--------------------------------------------------------------------------------
-- 状態機械コアロジック (純粋関数)
--------------------------------------------------------------------------------

toggleMute :: MuteMode -> MuteMode
toggleMute ModeNormal     = ModeMuteOnce
toggleMute ModeMuteOnce   = ModePersistent
toggleMute ModePersistent = ModeNormal

stepMonitor
  :: Bool
  -> Int
  -> DeltaG
  -> Int
  -> Bool
  -> Millis
  -> MonitorContext
  -> (BuzzerCmd, MonitorContext)
stepMonitor hasPkt pktRawStatus pktDelta pktBat btnB now ctx =
  let
    modeAfterBtn = if btnB then toggleMute (muteMode ctx) else muteMode ctx
    isTimeout = (now - lastPacketTime ctx) > 3000

    (newStatus, newPktTime, newDelta, newStickBat) =
      if hasPkt then
        let s = if pktRawStatus == 1 then StatusRunning else StatusStopped
        in (s, now, pktDelta, pktBat)
      else if isTimeout then
        (StatusDisconnected, lastPacketTime ctx, lastDelta ctx, lastStickBat ctx)
      else
        (currentStatus ctx, lastPacketTime ctx, lastDelta ctx, lastStickBat ctx)

    finalMode =
      if currentStatus ctx == StatusStopped && newStatus == StatusRunning && modeAfterBtn == ModeMuteOnce
      then ModeNormal
      else modeAfterBtn

    buzzerCmd
      | currentStatus ctx /= StatusDisconnected && newStatus == StatusDisconnected = 3
      | currentStatus ctx == StatusDisconnected && newStatus /= StatusDisconnected = 2
      | newStatus == StatusStopped && finalMode == ModeNormal                      = 1
      | otherwise                                                                  = 0

    nextCtx = MonitorContext
      { currentStatus  = newStatus
      , muteMode       = finalMode
      , lastPacketTime = newPktTime
      , lastDelta      = newDelta
      , lastStickBat   = newStickBat
      }
  in (buzzerCmd, nextCtx)

--------------------------------------------------------------------------------
-- メインループ
--------------------------------------------------------------------------------

loop :: MonitorContext -> IO ()
loop ctx = do
  now      <- c_get_millis
  hasPktI  <- c_poll_has_packet
  rawStat  <- c_get_packet_status
  pktDelta <- c_get_packet_delta
  pktBat   <- c_get_packet_battery
  btnBI    <- c_poll_btn_b_pressed
  core2Bat <- c_get_core2_battery

  let (buzzerCmd, nextCtx) = stepMonitor (hasPktI /= 0) rawStat pktDelta pktBat (btnBI /= 0) now ctx

  -- C++ 側へ状態と数値をそのまま渡す
  c_render_template
    (statusToInt (currentStatus nextCtx))
    (muteModeToInt (muteMode nextCtx))
    (lastDelta nextCtx)
    (lastStickBat nextCtx)
    core2Bat

  c_set_buzzer buzzerCmd
  c_delay_ms 50
  loop nextCtx

main :: IO ()
main = do
  now <- c_get_millis
  let initCtx = MonitorContext
        { currentStatus  = StatusDisconnected
        -- 起動時の初期状態を「永続ミュート」に変更 (机上テスト時の騒音防止)
        , muteMode       = ModePersistent
        , lastPacketTime = now
        , lastDelta      = 0.0
        , lastStickBat   = 0
        }
  loop initCtx
