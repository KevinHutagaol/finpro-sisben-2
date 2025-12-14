import { signOut } from "firebase/auth";
import { ref, set } from "firebase/database";
import { useObject } from "react-firebase-hooks/database";
import { auth, db } from "./firebase"; // Removed .ts
import styles from "./Dashboard.module.css";

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
    online: boolean;
}

export default function Dashboard() {
    // 1. Fetch data
    const [snapshot, loading, error] = useObject(ref(db, "device_001"));

    // 2. Handle Loading/Error
    if (loading) {
        return <div className={styles.centerMessage}><h2>Connecting to ESP32...</h2></div>
    }

    if (error) {
        return <div className={styles.centerMessage}><h2>Error: {error.message}</h2></div>
    }

    // 3. Process Data safely
    const deviceData = snapshot?.val() as DeviceData | null;

    const status = deviceData?.status || {
        is_locked: false,
        door_closed: false,
        alarm_triggered: false,
    };

    const control = deviceData?.control || {
        set_lock: false,
    };

    // New: Handle Online Status (default to false if data missing)
    const isOnline = deviceData?.online ?? false;

    const toggleLock = () => {
        set(ref(db, "device_001/control/set_lock"), !control.set_lock)
            .then(() => {})
            .catch((err) => alert(err.message));
    }

    const isDoorOpen = !status.door_closed;

    return (
        <div className={styles.container}>
            <div className={styles.dashboardCard}>

                {/* Header Section */}
                <header className={styles.header}>
                    <div className={styles.titleGroup}>
                        <h2 className={styles.title}>Device Control: 001</h2>

                        {/* Online/Offline Indicator */}
                        <div className={`${styles.onlineBadge} ${isOnline ? styles.online : styles.offline}`}>
                            <span className={styles.dot}></span>
                            {isOnline ? "Online" : "Offline"}
                        </div>
                    </div>

                    <button onClick={() => signOut(auth)} className={styles.logoutButton}>
                        Log Out
                    </button>
                </header>

                {/* Status Grid Section */}
                <div className={styles.statusGrid}>
                    <StatusIndicator
                        label="Lock Status"
                        isActive={status.is_locked}
                        color={status.is_locked ? "#4caf50" : "#ff9800"} // Green vs Orange
                        activeText="LOCKED"
                        inactiveText="UNLOCKED"
                    />
                    <StatusIndicator
                        label="Door Sensor"
                        isActive={status.door_closed}
                        color={status.door_closed ? "#4caf50" : "#ef5350"} // Green vs Red
                        activeText="CLOSED"
                        inactiveText="OPEN"
                    />
                    <StatusIndicator
                        label="Alarm System"
                        isActive={status.alarm_triggered}
                        color={status.alarm_triggered ? "#d32f2f" : "#9e9e9e"} // Red vs Grey
                        activeText="TRIGGERED"
                        inactiveText="SAFE"
                    />
                </div>

                {/* Actions Section */}
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
                        disabled={isDoorOpen || !isOnline} // Also disable if offline
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

// ---------------------------------------------------------
// Sub-component for individual status cards
// ---------------------------------------------------------

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
                {/* Visual indicator (simple dot/circle) */}
                <div style={{ width: "12px", height: "12px", background: "white", borderRadius: "50%" }}></div>
            </div>
            <span className={styles.statusLabel}>{label}</span>
            <div className={styles.statusValue}>
                {isActive ? activeText : inactiveText}
            </div>
        </div>
    );
}