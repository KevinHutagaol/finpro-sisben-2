import { useState } from "react";
import {signInWithEmailAndPassword} from "firebase/auth";
import { auth } from "./firebase.ts";

export default function Login() {
    const [email, setEmail] = useState("");
    const [password, setPassword] = useState("");
    const [error, setError] = useState("");

    const handleLogin = async (e: any) => {
        e.preventDefault();
        try {
            await signInWithEmailAndPassword(auth, email, password);
        } catch (error: any) {
            setError(error.message);
        }
    }
    
    return (
        <div style={{ padding: "20px" }}>
            <h2>Login</h2>
            <form onSubmit={handleLogin}>
                <label htmlFor="email">Email</label>
                <input name="email" type="email" placeholder="myemail@gmail.com" value={email} onChange={(e) => setEmail(e.target.value)} />

                <br/>

                <label htmlFor="password">Password</label>
                <input type="password" placeholder="12345" value={password} onChange={(e) => setPassword(e.target.value)} />

                <br/>

                <button type="submit">Log In</button>
            </form>
            {error && <p style={{color: "red"}}> {error} </p> }
        </div>
    )
}