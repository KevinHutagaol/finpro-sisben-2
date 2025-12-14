import {useEffect, useState} from "react";
import { signInWithEmailAndPassword } from "firebase/auth";
import { auth } from "./firebase"; // Removed .ts extension for import usually
import styles from "./Login.module.css";

export default function Login() {
    useEffect(() => {
        // This will run when the page loads
        document.title = "Login | Smart Home Door Lock";
    }, []);

    const [email, setEmail] = useState("");
    const [password, setPassword] = useState("");
    const [error, setError] = useState("");

    // Replace these strings with your actual details
    const studentName = "Kevin Imanuel Hutagaol";
    const studentID = "2306156763";

    const handleLogin = async (e: React.FormEvent) => {
        e.preventDefault();
        setError(""); // Clear previous errors
        try {
            await signInWithEmailAndPassword(auth, email, password);
        } catch (error: any) {
            // Clean up firebase error messages for UI
            const cleanMessage = error.message.replace("Firebase: ", "").replace(" (auth/invalid-credential).", "");
            setError("Login failed: " + cleanMessage);
        }
    }

    return (
        <div className={styles.container}>
            <div className={styles.card}>
                {/* Class Project Header */}
                <div className={styles.projectInfo}>
                    <h1 className={styles.projectName}>IoT Smart Home Security System with ESP32</h1>
                    <p className={styles.courseInfo}>Embedded Systems 2</p>
                    <p className={styles.studentInfo}>{studentName} | {studentID}</p>
                </div>

                <h2 className={styles.title}>Access Dashboard</h2>

                <form onSubmit={handleLogin} className={styles.form}>
                    <div>
                        <label htmlFor="email" className={styles.label}>Email Address</label>
                        <input
                            id="email"
                            name="email"
                            type="email"
                            className={styles.input}
                            placeholder="admin@smarthome.com"
                            value={email}
                            onChange={(e) => setEmail(e.target.value)}
                            required
                        />
                    </div>

                    <div>
                        <label htmlFor="password" className={styles.label}>Password</label>
                        <input
                            id="password"
                            type="password"
                            className={styles.input}
                            placeholder="••••••••"
                            value={password}
                            onChange={(e) => setPassword(e.target.value)}
                            required
                        />
                    </div>

                    <button type="submit" className={styles.button}>Log In</button>
                </form>

                {error && <div className={styles.error}>{error}</div>}
            </div>
        </div>
    )
}