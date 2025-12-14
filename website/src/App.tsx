import { useAuthState } from "react-firebase-hooks/auth";
import { auth } from "./firebase.ts";
import Login from "./Login.tsx";
import Dashboard from "./Dashboard";
import styles from "./App.module.css";

export default function App() {
    const [user, loading] = useAuthState(auth);

    if (loading) return <h1>Loading.....</h1>;

    return (
        <div className={styles.App}>
            {user ? <Dashboard/> : <Login/> }
        </div>
    )
}