import { signOut } from "firebase/auth";
import { ref, set } from "firebase/database";
import { useObject } from "react-firebase-hooks/database"
import { auth, db } from "./firebase.ts"


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
}

export default function Dashboard() {
    const [snaphot, loading, error] = useObject(ref(db, "device_001"));

    if (loading) {
        return (
            <h1>Loading...</h1>
        )
    }

    if (error) {
        return (
            <h1>Error: {error.message}</h1>
        )
    }

    const deviceData = snaphot?.val() as DeviceData | null;
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
            .then(() => {})
    }

    const isDoorOpen = status.door_closed === false


    return (
        <div style={{padding: "20px", fontFamily: "sans-serif"}}>
            <header style={{display: "flex", justifyContent: "space-between", alignItems: "center"}}>
                <h2>Device Control: 001</h2>
                <button onClick={() => signOut(auth)}>Log Out</button>
            </header>

            <hr/>

            <div style={{display: "flex", gap: "20px", marginBottom: "30px"}}>
                <StatusIndicator
                    label="Lock Status"
                    isActive={status.is_locked}
                    activeColor="green"
                    inactiveColor="orange"
                    activeText="LOCKED"
                    inactiveText="UNLOCKED"
                />
                <StatusIndicator
                    label="Door Sensor"
                    isActive={status.door_closed}
                    activeColor="green"
                    inactiveColor="red"
                    activeText="CLOSED"
                    inactiveText="OPEN"
                />
                <StatusIndicator
                    label="Alarm"
                    isActive={status.alarm_triggered}
                    activeColor="red"
                    inactiveColor="gray"
                    activeText="TRIGGERED"
                    inactiveText="SAFE"
                />
            </div>

            <div style={{padding: "20px", border: "1px solid #ccc", borderRadius: "8px"}}>
                <h3>Actions</h3>

                {isDoorOpen && <p style={{color: "red"}}>⚠️ Cannot lock while door is open</p>}

                <button
                    onClick={toggleLock}
                    disabled={isDoorOpen}
                    style={{
                        padding: "15px 30px",
                        fontSize: "18px",
                        cursor: isDoorOpen ? "not-allowed" : "pointer",
                        backgroundColor: control.set_lock ? "#e74c3c" : "#2ecc71",
                        color: "white",
                        border: "none",
                        borderRadius: "5px",
                        opacity: isDoorOpen ? 0.5 : 1
                    }}
                >
                    {control.set_lock ? "UNLOCK DOOR" : "LOCK DOOR"}
                </button>
            </div>

        </div>
    )

}

interface StatusProps {
    label: string;
    isActive: boolean;
    activeColor: string;
    inactiveColor: string;
    activeText: string;
    inactiveText: string;
}

function StatusIndicator({ label, isActive, activeColor, inactiveColor, activeText, inactiveText }: StatusProps) {
    return (
        <div style={{ textAlign: "center" }}>
            <div
                style={{
                    width: "50px",
                    height: "50px",
                    borderRadius: "50%",
                    backgroundColor: isActive ? activeColor : inactiveColor,
                    margin: "0 auto 10px auto",
                    border: "2px solid #333"
                }}
            />
            <strong>{label}</strong>
            <br />
            <span>{isActive ? activeText : inactiveText}</span>
        </div>
    );
}