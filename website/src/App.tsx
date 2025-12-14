import { useAuthState} from "react-firebase-hooks/auth";
import { auth } from "./firebase.ts";
import Login from "./Login.tsx";
import Dashboard from "./Dashboard";

export default function App() {
    const [user, loading] = useAuthState(auth);

    if (loading) return <h1>Loading.....</h1>;

    return (
        <div className="App">
            {user ? <Dashboard/> : <Login/> }
        </div>
    )
}