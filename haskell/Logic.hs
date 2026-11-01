{-|
Module      : Main
Description : M5StickC Plus2 振動検知・稼働状態判定エンジン
Maintainer  : info@kuono.net

M5StickC Plus2 の 6軸 IMU (SH200Q / MPU6886) から得られる加速度差分を基に、
対象機器が「稼働中 (Running)」か「停止中 (Stopped)」かを純粋関数によって判定します。
-}
{-|
Module      : Main
Description : M5StickC Plus2 振動センサ側 純粋状態機械 (オフディレイ保持)
-}
module Main where

type DeltaG = Float
type Millis = Int

-- 振動検知後のホールド時間 (2000ms = 2秒間)
holdDurationMs :: Millis
holdDurationMs = 2000

data SensorContext = SensorContext
  { lastVibeTime :: !Millis
  }

--------------------------------------------------------------------------------
-- C 言語 FFI 境界インターフェース
--------------------------------------------------------------------------------

foreign import ccall "c_get_millis"      c_get_millis      :: IO Millis
foreign import ccall "c_get_accel_delta" c_get_accel_delta :: IO DeltaG
foreign import ccall "c_get_threshold"   c_get_threshold   :: IO Float
foreign import ccall "c_send_packet"     c_send_packet     :: Int -> DeltaG -> IO ()
foreign import ccall "c_delay_ms"        c_delay_ms        :: Millis -> IO ()

--------------------------------------------------------------------------------
-- センサ判定コアロジック (純粋関数)
--------------------------------------------------------------------------------

-- | しきい値を超えたらタイマーを現在時刻に更新し、
--   ホールド時間内であれば RUNNING(1)、経過したら STOPPED(0) を出力
stepSensor :: DeltaG -> Float -> Millis -> SensorContext -> (Int, SensorContext)
stepSensor delta th now ctx =
  let
    vibratingNow    = delta >= th
    updatedVibeTime = if vibratingNow then now else lastVibeTime ctx
    isHoldActive    = (now - updatedVibeTime) <= holdDurationMs
    status          = if isHoldActive then 1 else 0
    nextCtx         = SensorContext { lastVibeTime = updatedVibeTime }
  in (status, nextCtx)

--------------------------------------------------------------------------------
-- メインループ
--------------------------------------------------------------------------------

loop :: SensorContext -> IO ()
loop ctx = do
  now   <- c_get_millis
  delta <- c_get_accel_delta
  th    <- c_get_threshold

  let (status, nextCtx) = stepSensor delta th now ctx
  c_send_packet status delta

  c_delay_ms 50
  loop nextCtx

main :: IO ()
main = do
  -- 起動直後は停止状態から開始
  loop (SensorContext { lastVibeTime = -99999 })