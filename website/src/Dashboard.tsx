import { signOut } from "firebase/auth";
import { ref, set, onValue } from "firebase/database";
import { useObject } from "react-firebase-hooks/database";
import { auth, db } from "./firebase";
import styles from "./Dashboard.module.css";
import { useEffect, useState } from "react";

// Types
type DeviceControl = {
    set_lock: boolean;
}

type DeviceStatus = {
    is_locked: boolean;
    door_closed: boolean;
    alarm_triggered: boolean;
}

type DeviceData = {
    control: DeviceControl;
    status: DeviceStatus;
    timestamp: number;
}

export default function Dashboard() {
    const [isOnline, setIsOnline] = useState(false);
    const [serverOffset, setServerOffset] = useState(0); // Store the time difference

    useEffect(() => {
        document.title = "Dashboard | Smart Home";

        const offsetRef = ref(db, ".info/serverTimeOffset");
        const unsub = onValue(offsetRef, (snap) => {
            setServerOffset(snap.val() || 0);
        });

        return () => unsub();
    }, []);

    const [snapshot, loading, error] = useObject(ref(db, "device_001"));

    const deviceData = snapshot?.val() as DeviceData | null;

    useEffect(() => {
        if (!deviceData?.timestamp) {
            setIsOnline(false);
            return;
        }

        const checkOnlineStatus = () => {
            const clientTime = Date.now();

            const estimatedServerTime = clientTime + serverOffset;

            const lastHeartbeat = deviceData.timestamp;
            const diff = estimatedServerTime - lastHeartbeat;

            setIsOnline(diff < 10000);
        };

        checkOnlineStatus();

        const interval = setInterval(checkOnlineStatus, 1000);

        return () => clearInterval(interval);
    }, [deviceData, serverOffset]);

    if (loading) return <div className={styles.centerMessage}><h2>Connecting...</h2></div>;
    if (error) return <div className={styles.centerMessage}><h2>Error: {error.message}</h2></div>;

    const status = deviceData?.status || {
        is_locked: false,
        door_closed: false,
        alarm_triggered: false,
    };

    const control = deviceData?.control || {
        set_lock: false,
    };

    const toggleLock = () => {
        set(ref(db, "device_001/control/set_lock"), !control.set_lock)
            .catch((err) => alert(err.message));
    }

    const isDoorOpen = !status.door_closed;

    return (
        <div className={styles.container}>
            <div className={styles.dashboardCard}>

                <header className={styles.header}>
                    <div className={styles.titleGroup}>
                        <h2 className={styles.title}>Device Control: 001</h2>

                        <div className={`${styles.onlineBadge} ${isOnline ? styles.online : styles.offline}`}>
                            <span className={styles.dot}></span>
                            {isOnline ? "Online" : "Offline"}
                        </div>
                    </div>

                    <button onClick={() => signOut(auth)} className={styles.logoutButton}>
                        Log Out
                    </button>
                </header>

                {/* Status Grid */}
                <div className={styles.statusGrid}>
                    <StatusIndicator
                        label="Lock Status"
                        isActive={status.is_locked}
                        color={status.is_locked ? "#4caf50" : "#ff9800"}
                        activeText="LOCKED"
                        inactiveText="UNLOCKED"
                    />
                    <StatusIndicator
                        label="Door Sensor"
                        isActive={status.door_closed}
                        color={status.door_closed ? "#4caf50" : "#ef5350"}
                        activeText="CLOSED"
                        inactiveText="OPEN"
                    />
                    <StatusIndicator
                        label="Alarm System"
                        isActive={status.alarm_triggered}
                        color={status.alarm_triggered ? "#d32f2f" : "#9e9e9e"}
                        activeText="TRIGGERED"
                        inactiveText="SAFE"
                    />
                </div>

                {/* Actions */}
                <div className={styles.actionSection}>
                    <h3 className={styles.actionTitle}>Remote Actions</h3>

                    {isDoorOpen && (
                        <div className={styles.warning}>
                            ⚠️ Cannot lock while door is open
                        </div>
                    )}

                    <br />

                    <button
                        onClick={toggleLock}
                        disabled={isDoorOpen || !isOnline}
                        className={`${styles.mainButton} ${control.set_lock ? styles.btnLocked : styles.btnUnlocked}`}
                    >
                        {!isOnline
                            ? "DEVICE OFFLINE"
                            : control.set_lock ? "UNLOCK DOOR" : "LOCK DOOR"
                        }
                    </button>
                </div>
            </div>
        </div>
    )
}

interface StatusProps {
    label: string;
    isActive: boolean;
    color: string;
    activeText: string;
    inactiveText: string;
}

function StatusIndicator({ label, isActive, color, activeText, inactiveText }: StatusProps) {
    return (
        <div className={styles.statusCard}>
            <div
                className={styles.statusIcon}
                style={{ backgroundColor: color }}
            >
                <div style={{ width: "12px", height: "12px", background: "white", borderRadius: "50%" }}></div>
            </div>
            <span className={styles.statusLabel}>{label}</span>
            <div className={styles.statusValue}>
                {isActive ? activeText : inactiveText}
            </div>
        </div>
    );
}